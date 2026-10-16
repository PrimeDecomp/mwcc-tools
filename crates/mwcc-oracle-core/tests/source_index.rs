use mwcc_oracle_core::{
    elf::Elf,
    evidence::EvaluationState,
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::{
        index::{self, Function, Ingest, Query, SourceRange},
        inputs::{FileInput, Snapshot},
        objective::Objective,
        search::Build,
        state,
    },
    version::Version,
};
use std::{fs, path::Path};

fn word(bytes: &mut [u8], at: usize, value: u32) {
    bytes[at..at + 4].copy_from_slice(&value.to_be_bytes());
}
fn half(bytes: &mut [u8], at: usize, value: u16) {
    bytes[at..at + 2].copy_from_slice(&value.to_be_bytes());
}
fn object() -> Vec<u8> {
    let strings = b"\0example__Fv\0symbol\0";
    let names = b"\0.text\0.strtab\0.symtab\0.rela.text\0.shstrtab\0";
    let mut bytes = vec![0; 52];
    bytes[..7].copy_from_slice(b"\x7fELF\x01\x02\x01");
    half(&mut bytes, 18, 20);
    let mut sections = vec![(0, 0, 0, 0, 0, 0, 0)];
    let at = bytes.len();
    bytes.extend_from_slice(&[0x38, 0x60, 0, 0, 0x4e, 0x80, 0, 0x20]);
    sections.push((1, 1, at, 8, 0, 0, 0));
    let at = bytes.len();
    bytes.extend_from_slice(strings);
    sections.push((7, 3, at, strings.len(), 0, 0, 0));
    let at = bytes.len();
    bytes.resize(at + 48, 0);
    word(&mut bytes, at + 16, 1);
    word(&mut bytes, at + 24, 8);
    bytes[at + 28] = 0x12;
    half(&mut bytes, at + 30, 1);
    word(&mut bytes, at + 32, 13);
    bytes[at + 44] = 0x10;
    sections.push((15, 2, at, 48, 2, 1, 16));
    let at = bytes.len();
    bytes.resize(at + 12, 0);
    word(&mut bytes, at, 2);
    word(&mut bytes, at + 4, (2 << 8) | 6);
    sections.push((23, 4, at, 12, 3, 1, 12));
    let at = bytes.len();
    bytes.extend_from_slice(names);
    sections.push((34, 3, at, names.len(), 0, 0, 0));
    let at = bytes.len();
    word(&mut bytes, 32, at as u32);
    half(&mut bytes, 46, 40);
    half(&mut bytes, 48, 6);
    half(&mut bytes, 50, 5);
    for (name, kind, offset, size, link, info, entry) in sections {
        let at = bytes.len();
        bytes.resize(at + 40, 0);
        for (field, value) in [
            (0, name),
            (4, kind),
            (16, offset),
            (20, size),
            (24, link),
            (28, info),
            (36, entry),
        ] {
            word(&mut bytes, at + field, value as u32);
        }
    }
    bytes
}
#[test]
fn exact_match_requires_both_bytes_and_symbolic_relocations() {
    let original = object();
    let evidence = index::exact_match(&original, &original, "example__Fv").unwrap();
    assert_eq!(evidence.relocations[0].symbol, "symbol");
    let mut changed = original.clone();
    changed[54] ^= 1;
    assert!(index::exact_match(&original, &changed, "example__Fv").is_err());
    let rela = Elf::parse(&original).unwrap().sections[4].data.as_ptr() as usize
        - original.as_ptr() as usize;
    for (at, value) in [(rela + 7, 4), (rela + 11, 1)] {
        let mut changed = original.clone();
        changed[at] = value;
        assert!(index::exact_match(&original, &changed, "example__Fv").is_err());
    }
    let strings = Elf::parse(&original).unwrap().sections[2].data.as_ptr() as usize
        - original.as_ptr() as usize;
    let mut changed = original.clone();
    changed[strings + 13] = b'S';
    assert!(index::exact_match(&original, &changed, "example__Fv").is_err());
    let symtab = Elf::parse(&original).unwrap().sections[3].data.as_ptr() as usize
        - original.as_ptr() as usize;
    let mut changed = original.clone();
    changed[symtab + 44] = 0x13;
    assert!(index::exact_match(&changed, &changed, "example__Fv").is_err());
    let mut changed = original.clone();
    let shoff = u32::from_be_bytes(changed[32..36].try_into().unwrap()) as usize;
    word(&mut changed, shoff + 4 * 40 + 4, 9);
    assert!(index::exact_match(&changed, &changed, "example__Fv").is_err());
}
fn write(path: &Path, bytes: &[u8]) -> String {
    fs::write(path, bytes).unwrap();
    state::digest(bytes)
}
#[test]
fn corpus_rejects_incomplete_absence_and_detects_tampering() {
    let temp = tempfile::tempdir().unwrap();
    let dir = temp.path().canonicalize().unwrap();
    let source = b"void example() {}\n";
    let source_path = dir.join("source.cpp");
    let source_sha = write(&source_path, source);
    let producer = dir.join("build/GC_2_7/oracle/mwcceppc.exe");
    fs::create_dir_all(producer.parent().unwrap()).unwrap();
    let producer_sha = write(&producer, b"producer");
    let stock = dir.join("build/compilers/GC/2.7/mwcceppc.exe");
    fs::create_dir_all(stock.parent().unwrap()).unwrap();
    let stock_sha = write(&stock, b"stock");
    let bytes = object();
    let object_path = dir.join("object.o");
    let object_sha = write(&object_path, &bytes);
    let target = dir.join("target.o");
    write(&target, &bytes);
    let options = TraceOptions {
        backend: true,
        ..Default::default()
    };
    let snapshot = Snapshot {
        format_version: 1,
        source: source_path.clone(),
        source_sha256: source_sha.clone(),
        working_directory: dir.clone(),
        flags: vec![],
        trace_options: serde_json::to_value(&options).unwrap(),
        runtime: vec![
            FileInput {
                path: producer,
                sha256: producer_sha.clone(),
            },
            FileInput {
                path: stock,
                sha256: stock_sha.clone(),
            },
        ],
        environment_sha256: "environment".into(),
        dependencies: vec![FileInput {
            path: source_path.clone(),
            sha256: source_sha.clone(),
        }],
        preprocessed_sha256: "oracle".into(),
        stock_preprocessed_sha256: "stock".into(),
        reusable: true,
        cache_limitations: vec![],
    };
    let compilation=Compilation {version:Version::Gc27,source:source_path.to_string_lossy().into_owned(),oracle:"oracle".into(),producer_sha256:producer_sha,original_sha256:Some(stock_sha),source_sha256:source_sha,flags:vec![],trace_options:options.clone(),object_sha256:object_sha,check_status:CheckStatus::Verified,identical:Some(true),differences:vec![],diagnostics:String::new(),trace:"pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=example__Fv\npv.function.end function=1 births=0 graphs=0 ranges=0 c0=4 c1=8 c2=32 c3=32 c4=32\n".into(),trace_present:true,original_fallback:false,object:vec![]};
    let capture = dir.join("capture.json");
    fs::write(
        &capture,
        serde_json::to_vec(&serde_json::json!({"compilation":compilation,"inputs":snapshot}))
            .unwrap(),
    )
    .unwrap();
    let build = Build {
        oracle_root: dir.clone(),
        working_directory: dir.clone(),
        source: source_path.to_string_lossy().into_owned(),
        version: Some(Version::Gc27),
        flags: vec!["-c".into(), "-MD".into()],
        from_ninja: None,
        traces: options,
        timeout_seconds: 10,
    };
    let spec = Ingest {
        format_version: 1,
        capture,
        object: object_path,
        target,
        source: source_path,
        snapshot: None,
        build,
        repository: "fixture".into(),
        revision: "declared-revision".into(),
        functions: vec![Function {
            link: "example__Fv".into(),
            source_range: SourceRange {
                first_line: 1,
                last_line: 1,
            },
        }],
    };
    let corpus = dir.join("index");
    let ingested = index::ingest(&corpus, &spec).unwrap();
    assert_eq!(ingested.added.len(), 1);
    let query = Query {
        format_version: 1,
        link: None,
        version: None,
        flags: None,
        class: 4,
        round: 0,
        objective: Objective::SymbolAddressPairs {
            symbol: "symbol".into(),
            minimum: 0,
            maximum: 0,
        },
        relocations: vec![],
    };
    let result = index::query(&corpus, &query).unwrap();
    assert!(result.hits.is_empty());
    assert_eq!(
        result.rejected[0]["evaluation"]["state"],
        serde_json::to_value(EvaluationState::Indeterminate).unwrap()
    );
    for filter in 0..3 {
        let mut wrong = query.clone();
        match filter {
            0 => wrong.link = Some("wrong".into()),
            1 => wrong.version = Some(Version::Gc13),
            _ => wrong.flags = Some(vec!["-O4,p".into()]),
        };
        assert!(index::query(&corpus, &wrong).unwrap().rejected.is_empty());
        let error = index::verify(
            &corpus,
            &ingested.added[0],
            &wrong,
            &dir.join(format!("verify-{filter}")),
            10,
            &dir.join("CANCEL"),
        )
        .unwrap_err();
        assert!(error.to_string().contains("does not match query"));
    }
    let entry = index::load(&corpus, &ingested.added[0]).unwrap();
    let cancel = dir.join("CANCEL");
    fs::write(&cancel, b"cancel").unwrap();
    let cancelled_output = dir.join("cancelled-verification");
    let error = index::verify(
        &corpus,
        "invalid-entry-id",
        &query,
        &cancelled_output,
        10,
        &cancel,
    )
    .unwrap_err();
    assert!(error.to_string().contains("cancelled"));
    assert!(!cancelled_output.exists());
    fs::remove_file(cancel).unwrap();
    fs::write(corpus.join("blobs").join(entry.source_sha256), b"tampered").unwrap();
    assert!(index::query(&corpus, &query).is_err());
    let path = corpus.join("entries").join(format!("{}.json", entry.id));
    let mut json: serde_json::Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    json["revision"] = "tampered".into();
    fs::write(path, serde_json::to_vec(&json).unwrap()).unwrap();
    assert!(index::load(&corpus, &entry.id).is_err());
}

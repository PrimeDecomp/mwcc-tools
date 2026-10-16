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
    let header_path = dir.join("example.hpp");
    let header = b"// declaration source\ninline void example() {}\n";
    let header_sha = write(&header_path, header);
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
        dependencies: vec![
            FileInput {
                path: source_path.clone(),
                sha256: source_sha.clone(),
            },
            FileInput {
                path: header_path.clone(),
                sha256: header_sha.clone(),
            },
        ],
        preprocessed_sha256: "oracle".into(),
        stock_preprocessed_sha256: "stock".into(),
        reusable: true,
        cache_limitations: vec![],
    };
    let compilation=Compilation {version:Version::Gc27,source:source_path.to_string_lossy().into_owned(),oracle:"oracle".into(),producer_sha256:producer_sha,original_sha256:Some(stock_sha),source_sha256:source_sha,flags:vec![],trace_options:options.clone(),object_sha256:object_sha,check_status:CheckStatus::Verified,identical:Some(true),differences:vec![],diagnostics:String::new(),trace:"pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=example__Fv\npv.function.end function=1 births=0 graphs=0 ranges=0 c0=4 c1=8 c2=32 c3=32 c4=32\n".into(),trace_present:true,original_fallback:false,object:vec![]};
    let mut compilation = compilation;
    compilation.trace = format!(
        "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes array_promotion=guarded_scalar_v1 call_result=guarded_call_result_v1 link=example__Fv\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=574005 expression=0 phase=1 line=1 category=unknown\npv.value.array schema=1 function=1 value=1 promotion=100 object=200 register=32 element_index=1 element_size=4 array_size=8 element_count=2 flags=0 datatype=1 storage=0 type_kind=12 type_size=8 name_known=1 name_complete=1 name_hex=666c6167\npv.value.birth schema=1 function=1 value=2 cls=4 register=33 count=34 physical=32 site=52881d expression=0 phase=0 line=1 category=unknown\npv.value.call_result schema=1 function=1 value=2 part=1 register=33 item=300 kind=54 callee=400 return_type=500 type_kind=8 type_size=4 output=600 output_kind=0 output_complete=0 output_hex={} tree_known=1 tree_complete=0 tree_hex=63616c6c2829\npv.function.end function=1 births=2 graphs=0 ranges=0 array_promotions=1 call_results=1 c0=4 c1=8 c2=32 c3=32 c4=34\n",
        "00".repeat(24)
    );
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
            source_file: None,
        }],
    };
    let corpus = dir.join("index");
    let ingested = index::ingest(&corpus, &spec).unwrap();
    assert_eq!(ingested.added.len(), 1);
    let stored = index::load(&corpus, &ingested.added[0]).unwrap();
    assert!(
        serde_json::to_value(&stored).unwrap()["function"]
            .get("source_file")
            .is_none()
    );
    assert_eq!(stored.coverage["origins"]["scalar_arrays"]["observed"], 1);
    let mut legacy = stored.clone();
    legacy.coverage.as_object_mut().unwrap().remove("origins");
    legacy.id.clear();
    legacy.id = state::digest(&serde_json::to_vec(&legacy).unwrap());
    fs::write(
        corpus.join("entries").join(format!("{}.json", legacy.id)),
        serde_json::to_vec_pretty(&legacy).unwrap(),
    )
    .unwrap();
    let inventory = index::origins(&corpus, &index::OriginFilter::default()).unwrap();
    assert_eq!(inventory.entries.len(), 2);
    for entry in &inventory.entries {
        assert_eq!(entry.source, "void example() {}\n");
        assert_eq!(
            entry.coverage.scalar_arrays.observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(entry.coverage.scalar_arrays.observed, 1);
        assert_eq!(
            entry.scalar_arrays[0].origin.name_text.as_deref(),
            Some("flag")
        );
        assert_eq!(
            (
                entry.scalar_arrays[0].origin.element_index,
                entry.scalar_arrays[0].origin.element_size
            ),
            (1, 4)
        );
        assert_eq!(entry.coverage.call_results.observed, 1);
        assert_eq!(entry.call_results[0].origin.callee, "400");
        assert!(!entry.call_results[0].origin.output_complete);
        assert!(!entry.call_results[0].origin.tree_complete);
        assert!(entry.coverage.normalize_symbols.scope.is_none());
        assert_eq!(
            entry.coverage.normalize_symbols.observations.state,
            EvaluationState::Indeterminate
        );
    }
    let mut header_spec = spec.clone();
    header_spec.functions[0].source_file = Some(FileInput {
        path: header_path.clone(),
        sha256: header_sha.clone(),
    });
    header_spec.functions[0].source_range = SourceRange {
        first_line: 2,
        last_line: 2,
    };
    let header_corpus = dir.join("header-index");
    let header_ingested = index::ingest(&header_corpus, &header_spec).unwrap();
    let header_entry = index::load(&header_corpus, &header_ingested.added[0]).unwrap();
    assert_eq!(header_entry.build.source, spec.build.source);
    assert_eq!(header_entry.inputs.source, spec.source);
    let header_inventory = index::origins(&header_corpus, &index::OriginFilter::default()).unwrap();
    let item = &header_inventory.entries[0];
    assert_eq!(item.source, "inline void example() {}\n");
    assert_eq!(item.source_file, header_path);
    assert_eq!(item.source_sha256, header_sha);
    assert_eq!(item.translation_unit_source_sha256, stored.source_sha256);
    let mut invalid = header_spec.clone();
    invalid.functions[0].source_range.last_line = 3;
    assert!(
        index::ingest(&dir.join("bad-range"), &invalid)
            .unwrap_err()
            .to_string()
            .contains("range outside")
    );
    let outside = dir.join("outside.hpp");
    write(&outside, header);
    invalid = header_spec.clone();
    invalid.functions[0].source_file.as_mut().unwrap().path = outside;
    assert!(
        index::ingest(&dir.join("outside"), &invalid)
            .unwrap_err()
            .to_string()
            .contains("dependency closure")
    );
    invalid = header_spec;
    invalid.functions[0].source_file.as_mut().unwrap().sha256 = state::digest(b"wrong");
    assert!(
        index::ingest(&dir.join("wrong-digest"), &invalid)
            .unwrap_err()
            .to_string()
            .contains("different digest")
    );
    fs::write(
        header_corpus.join("blobs").join(&header_sha),
        b"tampered header",
    )
    .unwrap();
    assert!(index::origins(&header_corpus, &index::OriginFilter::default()).is_err());
    assert!(
        index::origins(
            &corpus,
            &index::OriginFilter {
                version: Some(Version::Gc13),
                ..Default::default()
            }
        )
        .unwrap()
        .entries
        .is_empty()
    );
    assert!(
        index::origins(
            &corpus,
            &index::OriginFilter {
                link: Some("wrong".into()),
                ..Default::default()
            }
        )
        .unwrap()
        .entries
        .is_empty()
    );
    assert!(
        index::origins(
            &corpus,
            &index::OriginFilter {
                flags: Some(vec!["wrong".into()]),
                ..Default::default()
            }
        )
        .unwrap()
        .entries
        .is_empty()
    );
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

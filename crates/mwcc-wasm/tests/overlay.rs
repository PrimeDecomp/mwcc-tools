//! The file overlay and sjiswrap's transcoding, compiling with this checkout's GC 2.7 module (skipped when it is not
//! built). MWCC_TEST_ECHOES (an Echoes checkout, on Windows) adds the compare with sjiswrap and the executable.
use mwcc_wasm::{Outcome, Overlay, Run};
use std::{
    fs,
    path::{Path, PathBuf},
    time::Duration,
};

fn module() -> Option<PathBuf> {
    let module =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/GC_2_7/wasm/mwcc.wasm");
    if module.is_file() {
        Some(module)
    } else {
        eprintln!("(skipped: the GC 2.7 module is not built)");
        None
    }
}

const FLAGS: &[&str] = &[
    "-nodefaults",
    "-proc",
    "gekko",
    "-O4,p",
    "-i",
    "inc",
    "-c",
    "a.c",
    "-o",
    "a.o",
];

/// A.C compiled in DIRECTORY with FLAGS and EXTRA, through OVERLAY: the outcome, and the object (if written).
fn compile(
    module: &Path,
    directory: &Path,
    extra: &[&str],
    overlay: Option<&Overlay>,
    transcode_sjis: bool,
) -> (Outcome, Option<Vec<u8>>) {
    let _ = fs::remove_file(directory.join("a.o"));
    let args: Vec<String> = FLAGS.iter().chain(extra).map(|a| a.to_string()).collect();
    let program = directory.join("mwcceppc.exe").display().to_string();
    let request = Run {
        args: &args,
        directory,
        environment: &[],
        program: &program,
        version_info: &[],
        timeout: Some(Duration::from_secs(60)),
        cancel: None,
        trace: false,
        overlay,
        transcode_sjis,
        record_reads: true,
        command_line: None,
        shared_files: None,
    };
    // (a module without version information beside it, as the stock one: compiled once to its cache)
    let outcome = if module.with_extension("version").is_file() {
        mwcc_wasm::load(module).unwrap().run(&request, &|| false)
    } else {
        mwcc_wasm::Compiler::new_cached(&fs::read(module).unwrap(), &module.with_extension("cache"))
            .unwrap()
            .run(&request)
    }
    .unwrap();
    let object = fs::read(directory.join("a.o")).ok();
    (outcome, object)
}

fn console(outcome: &Outcome) -> String {
    String::from_utf8_lossy(&outcome.console).into_owned()
}

fn project(header: &str) -> tempfile::TempDir {
    let work = tempfile::tempdir().unwrap();
    fs::create_dir(work.path().join("inc")).unwrap();
    fs::write(work.path().join("inc/v.h"), header).unwrap();
    fs::write(
        work.path().join("a.c"),
        "#include \"v.h\"\nint f(void) { return V; }\n",
    )
    .unwrap();
    work
}

#[test]
fn an_overlaid_header_shadows_the_disk_file() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let mut overlay = Overlay::new();
    // (spelled in another case, and with `..`)
    overlay.insert(
        work.path().join("x/../INC/V.H"),
        b"#define V 1234\n".as_slice(),
    );
    let (outcome, overlaid) = compile(&module, work.path(), &[], Some(&overlay), false);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    let (_, plain) = compile(&module, work.path(), &[], None, false);
    assert_ne!(overlaid, plain);
    fs::write(work.path().join("inc/v.h"), "#define V 1234\n").unwrap();
    let (_, on_disk) = compile(&module, work.path(), &[], None, false);
    assert_eq!(overlaid, on_disk);
}

#[test]
fn an_overlay_only_header_and_source_are_read() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let mut overlay = Overlay::new();
    overlay.insert(work.path().join("inc/w.h"), b"#define W 77\n".as_slice());
    overlay.insert(
        work.path().join("a.c"),
        b"#include \"w.h\"\nint f(void) { return W; }\n".as_slice(),
    );
    let (outcome, overlaid) = compile(&module, work.path(), &[], Some(&overlay), false);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    fs::write(work.path().join("inc/w.h"), "#define W 77\n").unwrap();
    fs::write(
        work.path().join("a.c"),
        "#include \"w.h\"\nint f(void) { return W; }\n",
    )
    .unwrap();
    let (_, on_disk) = compile(&module, work.path(), &[], None, false);
    assert!(overlaid.is_some());
    assert_eq!(overlaid, on_disk);
}

#[test]
fn an_absent_header_is_not_found() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let mut overlay = Overlay::new();
    overlay.insert_absent(work.path().join("inc/v.h"));
    let (outcome, object) = compile(&module, work.path(), &[], Some(&overlay), false);
    assert_ne!(outcome.exit_code, 0);
    assert!(object.is_none());
    assert!(console(&outcome).contains("v.h"), "{}", console(&outcome));
}

#[test]
fn writes_to_an_overlaid_path_are_refused() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let mut overlay = Overlay::new();
    overlay.insert(work.path().join("a.o"), b"not an object".as_slice());
    let (outcome, object) = compile(&module, work.path(), &[], Some(&overlay), false);
    assert_ne!(outcome.exit_code, 0, "{}", console(&outcome));
    assert!(object.is_none());
}

/// A literal whose second byte in Shift JIS is a backslash (表, 0x95 0x5C) and a kana one.
const SJIS_SOURCE: &str =
    "const char* s = \"表ソ\";\nconst char* t = \"ゼルダ\";\nint f(void) { return s[1] + t[2]; }\n";

#[test]
fn utf8_sources_compile_as_shift_jis() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    fs::write(work.path().join("a.c"), SJIS_SOURCE).unwrap();
    let (outcome, utf8) = compile(&module, work.path(), &["-multibyte"], None, true);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    let utf8 = utf8.unwrap();
    // (the object has the Shift JIS bytes, the backslash one with its lead byte)
    assert!(utf8.windows(4).any(|w| w == b"\x95\x5c\x83\x5c"));
    // (and from an overlay, the same)
    fs::write(work.path().join("a.c"), "int unrelated;\n").unwrap();
    let mut overlay = Overlay::new();
    overlay.insert(work.path().join("a.c"), SJIS_SOURCE.as_bytes());
    let (_, overlaid) = compile(&module, work.path(), &["-multibyte"], Some(&overlay), true);
    assert_eq!(overlaid.as_ref(), Some(&utf8));
    // (a source that is not UTF-8 ends the run with sjiswrap's error)
    fs::write(work.path().join("a.c"), b"char c = '\x95\x5c';\n").unwrap();
    let (outcome, object) = compile(&module, work.path(), &["-multibyte"], None, true);
    assert_ne!(outcome.exit_code, 0);
    assert!(object.is_none());
    assert!(
        console(&outcome).contains("is not valid UTF-8"),
        "{}",
        console(&outcome)
    );
}

#[test]
fn shift_jis_objects_equal_sjiswrap_and_the_executable() {
    let Some(module) = module() else { return };
    let Some(echoes) = std::env::var_os("MWCC_TEST_ECHOES").map(PathBuf::from) else {
        eprintln!("(skipped: MWCC_TEST_ECHOES is not set)");
        return;
    };
    if !cfg!(windows) {
        return;
    }
    let work = project("#define V 1\n");
    fs::write(work.path().join("a.c"), SJIS_SOURCE).unwrap();
    let (outcome, ours) = compile(&module, work.path(), &["-multibyte"], None, true);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    let _ = fs::remove_file(work.path().join("a.o"));
    let status = std::process::Command::new(echoes.join("build/tools/sjiswrap.exe"))
        .arg(echoes.join("build/compilers/GC/2.7/mwcceppc.exe"))
        .args(FLAGS)
        .arg("-multibyte")
        .current_dir(work.path())
        .status()
        .unwrap();
    assert!(status.success());
    assert_eq!(ours, fs::read(work.path().join("a.o")).ok());
}

#[test]
fn a_persistent_compile_reads_the_overlay() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let capacity = 4096;
    let context = "#include \"v.h\"\n";
    let source = mwcc_wasm::persistent_source(context, capacity);
    fs::write(work.path().join("a.c"), &source).unwrap();
    let mut overlay = Overlay::new();
    overlay.insert(work.path().join("inc/v.h"), b"#define V 1234\n".as_slice());
    let loaded = mwcc_wasm::load(&module).unwrap();
    let args: Vec<String> = FLAGS.iter().map(|a| a.to_string()).collect();
    let program = work.path().join("mwcceppc.exe").display().to_string();
    let mut persistent = loaded
        .compiler
        .persistent(
            &Run {
                args: &args,
                directory: work.path(),
                environment: &[],
                program: &program,
                version_info: &loaded.version_info,
                timeout: Some(Duration::from_secs(60)),
                cancel: None,
                trace: false,
                overlay: Some(&overlay),
                transcode_sjis: false,
                record_reads: false,
                command_line: None,
                shared_files: None,
            },
            capacity,
        )
        .unwrap();
    let candidate = b"int f(void) { return V; }\n";
    let fast = persistent
        .compile(candidate, Some(Duration::from_secs(60)))
        .unwrap();
    assert_eq!(
        fast.exit_code,
        0,
        "{}",
        String::from_utf8_lossy(&fast.console)
    );
    // (the same source with the candidate in its slot, compiled normally through the same overlay: by the module
    // without persist, as the marker is a checkpoint in this one)
    let stock = module.with_file_name("mwcceppc.wasm");
    let empty = mwcc_wasm::slot_text(b"", capacity).unwrap();
    let mut filled = source[..source.len() - empty.len()].to_vec();
    filled.extend(mwcc_wasm::slot_text(candidate, capacity).unwrap());
    overlay.insert(work.path().join("a.c"), filled);
    let (outcome, normal) = compile(&stock, work.path(), &[], Some(&overlay), false);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    assert_eq!(fast.object, normal);
    let (_, without) = compile(&stock, work.path(), &[], None, false);
    assert_ne!(fast.object, without);
}

#[test]
fn a_persistent_candidate_is_read_as_shift_jis() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let capacity = 4096;
    let context = "#include \"v.h\"\n";
    let source = mwcc_wasm::persistent_source(context, capacity);
    fs::write(work.path().join("a.c"), &source).unwrap();
    let loaded = mwcc_wasm::load(&module).unwrap();
    let args: Vec<String> = FLAGS
        .iter()
        .chain(&["-multibyte"])
        .map(|a| a.to_string())
        .collect();
    let program = work.path().join("mwcceppc.exe").display().to_string();
    let mut persistent = loaded
        .compiler
        .persistent(
            &Run {
                args: &args,
                directory: work.path(),
                environment: &[],
                program: &program,
                version_info: &loaded.version_info,
                timeout: Some(Duration::from_secs(60)),
                cancel: None,
                trace: false,
                overlay: None,
                transcode_sjis: true,
                record_reads: false,
                command_line: None,
                shared_files: None,
            },
            capacity,
        )
        .unwrap();
    let fast = persistent
        .compile(SJIS_SOURCE.as_bytes(), Some(Duration::from_secs(60)))
        .unwrap();
    assert_eq!(
        fast.exit_code,
        0,
        "{}",
        String::from_utf8_lossy(&fast.console)
    );
    assert!(
        fast.object
            .as_ref()
            .unwrap()
            .windows(4)
            .any(|w| w == b"\x95\x5c\x83\x5c")
    );
    // (the source with the candidate in its slot, in UTF-8, compiled normally with transcoding)
    let empty = mwcc_wasm::slot_text(b"", capacity).unwrap();
    let encoded = encoding_rs::SHIFT_JIS.encode(SJIS_SOURCE).0.len();
    let padding = mwcc_wasm::slot_text(&vec![b'x'; encoded], capacity).unwrap()[encoded..].to_vec();
    let mut filled = source[..source.len() - empty.len()].to_vec();
    filled.extend(SJIS_SOURCE.as_bytes());
    filled.extend(padding);
    fs::write(work.path().join("a.c"), filled).unwrap();
    let stock = module.with_file_name("mwcceppc.wasm");
    let (outcome, normal) = compile(&stock, work.path(), &["-multibyte"], None, true);
    assert_eq!(outcome.exit_code, 0, "{}", console(&outcome));
    assert_eq!(fast.object, normal);
    // (a candidate that is not UTF-8 fails as sjiswrap fails)
    let failed = persistent.compile(b"char c = '\x95\x5c';\n", None).unwrap();
    assert_eq!(failed.exit_code, 1);
    assert!(String::from_utf8_lossy(&failed.console).contains("is not valid UTF-8"));
}

#[test]
fn the_outcome_lists_what_the_compiler_consulted() {
    let Some(module) = module() else { return };
    let work = project("#define V 1\n");
    let mut overlay = Overlay::new();
    overlay.insert(work.path().join("inc/w.h"), b"#define W 2\n".as_slice());
    fs::write(
        work.path().join("a.c"),
        "#include \"v.h\"\n#include \"w.h\"\nint f(void) { return V + W; }\n",
    )
    .unwrap();
    let (outcome, object) = compile(&module, work.path(), &[], Some(&overlay), false);
    assert!(object.is_some(), "{}", console(&outcome));
    let paths: Vec<String> = outcome
        .consulted
        .iter()
        .map(|c| match c {
            mwcc_wasm::Consulted::Path(p) | mwcc_wasm::Consulted::Listing(p) => p
                .strip_prefix(work.path())
                .unwrap_or(p)
                .to_string_lossy()
                .replace('\\', "/"),
        })
        .collect();
    // (the source, the header found, and each header looked for beside the source first; not the overlaid header)
    for wanted in ["a.c", "inc/v.h", "v.h"] {
        assert!(
            paths.iter().any(|p| p == wanted),
            "{wanted} not in {paths:?}"
        );
    }
    assert!(!paths.iter().any(|p| p == "inc/w.h"), "{paths:?}");
}

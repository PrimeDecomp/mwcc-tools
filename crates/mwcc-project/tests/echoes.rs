//! Units of an Echoes checkout (MWCC_TEST_ECHOES), compiled as its build compiles them, in process by this checkout's
//! GC 2.7 module (skipped without either).
use mwcc_project::{CompileCache, CompileOptions, Compiled, Overlay, Project, Unit, compile_unit};
use std::{
    fs,
    path::{Path, PathBuf},
    time::Duration,
};

fn oracle_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn echoes() -> Option<Project> {
    let Some(root) = std::env::var_os("MWCC_TEST_ECHOES") else {
        eprintln!("(skipped: MWCC_TEST_ECHOES is not set)");
        return None;
    };
    if !oracle_root()
        .join(mwcc_project::compilers::module_output("GC_2_7"))
        .is_file()
    {
        eprintln!("(skipped: the GC 2.7 module is not built)");
        return None;
    }
    Some(Project::open(Path::new(&root)).unwrap())
}

fn compile(unit: &Unit, overlay: &Overlay, root: &Path) -> Compiled {
    compile_cached(unit, overlay, root, None)
}

fn compile_cached(
    unit: &Unit,
    overlay: &Overlay,
    root: &Path,
    cache: Option<&CompileCache>,
) -> Compiled {
    let compiled = compile_unit(
        unit,
        overlay,
        &CompileOptions {
            oracle_root: root,
            inline_max_size: None,
            trace: None,
            timeout: Duration::from_secs(300),
            cancel: &|| false,
            cache,
            persist: None,
        },
    )
    .unwrap();
    assert_eq!(
        compiled.exit_code,
        0,
        "{}",
        String::from_utf8_lossy(&compiled.console)
    );
    compiled
}

#[test]
fn unmodified_units_compile_to_the_builds_objects() {
    let Some(project) = echoes() else { return };
    // (a DOL unit and a REL unit; every Echoes edge is mwcc_sjis)
    for name in [
        "main/MetroidPrime/CControlMapper",
        "AIMannedTurret/MetroidPrime/Enemies/CAIMannedTurret",
    ] {
        let unit = project.unit(name).unwrap();
        assert!(unit.sjiswrap.is_some(), "{name}");
        assert_eq!(unit.mw_version.as_deref(), Some("GC/2.7"));
        let compiled = compile(&unit, &Overlay::new(), &oracle_root());
        assert!(compiled.in_process, "{name} did not run in process");
        assert_eq!(compiled.object, fs::read(&unit.base_path).ok(), "{name}");
    }
}

#[test]
fn a_shift_jis_candidate_compiles_as_sjiswrap_and_the_executable_compile_it() {
    let Some(project) = echoes() else { return };
    let unit = project.unit("main/MetroidPrime/CControlMapper").unwrap();
    let mut source = fs::read_to_string(unit.source_path()).unwrap();
    // (表's second byte in Shift JIS is a backslash)
    source.push_str("\nextern const char* const mwcc_project_probe;\nconst char* const mwcc_project_probe = \"表ソゼルダ\";\n");
    let mut overlay = Overlay::new();
    overlay.insert(unit.source_path(), source.into_bytes());
    let ours = compile(&unit, &overlay, &oracle_root());
    assert!(ours.in_process);
    // (no module for the project's compiler in an empty root: the executable, through sjiswrap, on a scratch copy)
    let empty = tempfile::tempdir().unwrap();
    let theirs = compile(&unit, &overlay, empty.path());
    assert!(!theirs.in_process);
    assert!(ours.object.is_some());
    assert_eq!(ours.object, theirs.object);
    assert_ne!(ours.object, fs::read(&unit.base_path).ok());
}

#[test]
fn inline_max_size_replaces_the_flags_last() {
    let Some(project) = echoes() else { return };
    let unit = project.unit("main/MetroidPrime/CControlMapper").unwrap();
    let args = unit.arguments("a.cpp", "a.o", Some(77));
    assert!(args.iter().any(|a| a == "inline_max_size(77)"));
    assert!(!args.iter().any(|a| a == "inline_max_size(128)"));
    assert_eq!(&args[args.len() - 4..], ["-c", "a.cpp", "-o", "a.o"]);
}

#[test]
fn the_compile_cache_answers_a_compile_again_until_its_files_change() {
    let Some(project) = echoes() else { return };
    let unit = project.unit("main/MetroidPrime/CControlMapper").unwrap();
    let cache = CompileCache::default();
    let root = oracle_root();
    let first = compile_cached(&unit, &Overlay::new(), &root, Some(&cache));
    assert!(!first.cached);
    let again = compile_cached(&unit, &Overlay::new(), &root, Some(&cache));
    assert!(again.cached);
    assert_eq!(again.object, first.object);
    assert_eq!(again.console, first.console);
    // (another source: another key)
    let mut source = fs::read_to_string(unit.source_path()).unwrap();
    source.push_str(
        "
int mwcc_project_cache_probe;
",
    );
    let mut overlay = Overlay::new();
    overlay.insert(unit.source_path(), source.into_bytes());
    let changed = compile_cached(&unit, &overlay, &root, Some(&cache));
    assert!(!changed.cached);
    assert_ne!(changed.object, first.object);
    assert!(compile_cached(&unit, &overlay, &root, Some(&cache)).cached);
    assert_eq!((cache.hits(), cache.misses()), (2, 2));
}

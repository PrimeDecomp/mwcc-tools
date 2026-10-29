//! The persistent pool with this checkout's GC 2.7 module (skipped when it is not built): its objects and messages
//! equal normal compiles', failures with a diagnostic are trusted, a disagreeing normal compile retires the persistent
//! compiler, and the context's files are part of its identity.
use mwcc_wasm::{
    Overlay, PersistPool, PoolOptions, Run,
    pool::{Compiled, Context},
};
use std::{
    cell::Cell,
    fs,
    path::{Path, PathBuf},
    sync::Arc,
    time::{Duration, Instant},
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

const CONTEXT: &str = "#include \"v.h\"\ntypedef struct S { int a; float b; } S;\nstatic int g(int x) { return x * V; }\nextern S table[4];\n";

struct Project {
    work: tempfile::TempDir,
    context: Arc<Context>,
}

fn project(module: &Path, header: &str, overlay: Overlay) -> Project {
    let work = tempfile::tempdir().unwrap();
    fs::create_dir(work.path().join("inc")).unwrap();
    fs::write(work.path().join("inc/v.h"), header).unwrap();
    fs::write(work.path().join("a.c"), "").unwrap();
    let args: Vec<String> = [
        "-nodefaults",
        "-proc",
        "gekko",
        "-O4,p",
        "-sym",
        "on",
        "-i",
        "inc",
        "-c",
        "a.c",
        "-o",
        "a.o",
    ]
    .iter()
    .map(|a| a.to_string())
    .collect();
    let context = Arc::new(Context {
        module: mwcc_wasm::load(module).unwrap(),
        args,
        output: "a.o".into(),
        directory: work.path().to_path_buf(),
        environment: vec![],
        program: work.path().join("mwcceppc.exe").display().to_string(),
        source: work.path().join("a.c"),
        text: CONTEXT.into(),
        overlay,
        transcode_sjis: false,
    });
    Project { work, context }
}

/// The context and CANDIDATE compiled normally.
fn normal(project: &Project, candidate: &str) -> Compiled {
    let context = &project.context;
    let mut overlay = context.overlay.clone();
    overlay.insert(
        &context.source,
        format!("{}{candidate}", context.text).into_bytes(),
    );
    let output = context.directory.join(&context.output);
    let _ = fs::remove_file(&output);
    let outcome = context
        .module
        .run(
            &Run {
                args: &context.args,
                directory: &context.directory,
                environment: &context.environment,
                program: &context.program,
                version_info: &[],
                timeout: Some(Duration::from_secs(60)),
                cancel: None,
                trace: false,
                overlay: Some(&overlay),
                transcode_sjis: false,
                record_reads: false,
                command_line: None,
                shared_files: None,
            },
            &|| false,
        )
        .unwrap();
    Compiled {
        exit_code: outcome.exit_code,
        object: fs::read(&output).ok(),
        console: outcome.console,
        ..Compiled::default()
    }
}

fn candidate(i: usize) -> String {
    let mut text = format!("int f{i}(int n) {{\n    int s = 0;\n");
    for k in 0..(i % 5) {
        text.push_str(&format!("    s += g(n + {k}) - table[{}].a;\n", k % 4));
    }
    text.push_str("    return s;\n}\n");
    text
}

/// Compile CANDIDATE until a persistent compiler gives it (the first start is in the background).
fn until_fast(pool: &PersistPool, project: &Project, candidate: &str) -> Compiled {
    let started = Instant::now();
    loop {
        let compiled = pool
            .compile::<()>(
                &project.context,
                candidate.as_bytes(),
                Duration::from_secs(60),
                false,
                &|| Ok(normal(project, candidate)),
            )
            .unwrap();
        if compiled.fast {
            return compiled;
        }
        assert!(
            started.elapsed() < Duration::from_secs(60),
            "no persistent compiler started: {:?}",
            pool.stats()
        );
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[test]
fn persistent_results_equal_normal_compiles() {
    let Some(module) = module() else { return };
    let project = project(&module, "#define V 3\n", Overlay::new());
    let pool = PersistPool::new(PoolOptions::default());
    until_fast(&pool, &project, &candidate(0));
    for i in 0..20 {
        let text = candidate(i);
        let compiled = pool
            .compile::<()>(
                &project.context,
                text.as_bytes(),
                Duration::from_secs(60),
                false,
                &|| Ok(normal(&project, &text)),
            )
            .unwrap();
        assert!(compiled.fast, "candidate {i}");
        let reference = normal(&project, &text);
        assert_eq!(compiled.exit_code, reference.exit_code);
        assert!(
            compiled.object == reference.object,
            "candidate {i}'s object differs"
        );
    }
    let stats = pool.stats();
    assert_eq!(
        (stats.starts, stats.mismatches, stats.live),
        (1, 0, 1),
        "{stats:?}"
    );
}

#[test]
fn a_failure_with_a_diagnostic_is_trusted_and_a_recheck_comes_in_eight() {
    let Some(module) = module() else { return };
    let project = project(&module, "#define V 3\n", Overlay::new());
    let pool = PersistPool::new(PoolOptions::default());
    until_fast(&pool, &project, &candidate(0));
    let bad = "int e(void) {\n    return 1 +;\n}\n";
    let normals = Cell::new(0);
    let mut fast = 0;
    for _ in 0..8 {
        let compiled = pool
            .compile::<()>(
                &project.context,
                bad.as_bytes(),
                Duration::from_secs(60),
                false,
                &|| {
                    normals.set(normals.get() + 1);
                    Ok(normal(&project, bad))
                },
            )
            .unwrap();
        let reference = normal(&project, bad);
        assert_eq!(compiled.exit_code, 1);
        assert_eq!(
            String::from_utf8_lossy(&compiled.console),
            String::from_utf8_lossy(&reference.console)
        );
        fast += usize::from(compiled.fast);
    }
    assert_eq!((fast, normals.get()), (7, 1));
    // (with confirm_failures, every failure is compiled normally)
    let compiled = pool
        .compile::<()>(
            &project.context,
            bad.as_bytes(),
            Duration::from_secs(60),
            true,
            &|| {
                normals.set(normals.get() + 1);
                Ok(normal(&project, bad))
            },
        )
        .unwrap();
    assert!(!compiled.fast);
    assert_eq!(normals.get(), 2);
    let stats = pool.stats();
    assert_eq!(
        (stats.trusted_failures, stats.rechecked, stats.mismatches),
        (7, 2, 0),
        "{stats:?}"
    );
}

#[test]
fn a_normal_compile_that_disagrees_retires_the_persistent_compiler() {
    let Some(module) = module() else { return };
    let project = project(&module, "#define V 3\n", Overlay::new());
    let pool = PersistPool::new(PoolOptions::default());
    until_fast(&pool, &project, &candidate(0));
    assert_eq!(pool.stats().live, 1);
    let bad = "int e(void) { return 1 +; }\n";
    // (a normal compile that accepts it: the persistent compiler is not to be trusted)
    let compiled = pool
        .compile::<()>(
            &project.context,
            bad.as_bytes(),
            Duration::from_secs(60),
            true,
            &|| Ok(normal(&project, &candidate(1))),
        )
        .unwrap();
    assert_eq!(compiled.exit_code, 0);
    let stats = pool.stats();
    assert_eq!((stats.mismatches, stats.live), (1, 0), "{stats:?}");
}

#[test]
fn the_contexts_files_are_part_of_its_identity() {
    let Some(module) = module() else { return };
    let one = project(&module, "#define V 3\n", Overlay::new());
    let mut overlay = Overlay::new();
    overlay.insert(one.work.path().join("inc/v.h"), b"#define V 5\n".as_slice());
    let two = Project {
        context: Arc::new(Context {
            overlay,
            module: one.context.module.clone(),
            args: one.context.args.clone(),
            output: one.context.output.clone(),
            directory: one.context.directory.clone(),
            environment: vec![],
            program: one.context.program.clone(),
            source: one.context.source.clone(),
            text: CONTEXT.into(),
            transcode_sjis: false,
        }),
        work: tempfile::tempdir().unwrap(),
    };
    let pool = PersistPool::new(PoolOptions::default());
    let text = candidate(3);
    let a = until_fast(&pool, &one, &text);
    let b = until_fast(&pool, &two, &text);
    assert_ne!(a.object, b.object);
    assert_eq!(pool.stats().starts, 2);
}

#[test]
fn an_idle_persistent_compiler_is_dropped() {
    let Some(module) = module() else { return };
    let project = project(&module, "#define V 3\n", Overlay::new());
    let pool = PersistPool::new(PoolOptions {
        idle_timeout: Some(Duration::from_millis(200)),
        ..PoolOptions::default()
    });
    until_fast(&pool, &project, &candidate(0));
    assert_eq!(pool.stats().live, 1);
    let started = Instant::now();
    while pool.stats().live > 0 {
        assert!(
            started.elapsed() < Duration::from_secs(10),
            "{:?}",
            pool.stats()
        );
        std::thread::sleep(Duration::from_millis(20));
    }
}

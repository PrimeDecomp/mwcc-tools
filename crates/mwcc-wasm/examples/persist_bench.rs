//! Spike: persistent compiles (persist.wasm) against normal compiles (mwcceppc.wasm) of the same text.
//!
//! persist_bench MODULES_DIR EXE ROOT SOURCE FLAGS_FILE WORK [ROUNDS]
//!
//! SOURCE (a unit of the project at ROOT, compiled from ROOT) is split into its context (up to its last `#include`)
//! and its body, cut into top-level chunks. The candidates: the whole body, the body without each chunk, each chunk
//! alone, and a few edge cases. Each is compiled persistently and normally (the persistent source with the
//! candidate in its slot, same file name and flags); exit codes and objects must be identical.
use mwcc_wasm::{Compiler, Request, persistent_source, slot_text};
use std::{
    path::{Path, PathBuf},
    time::{Duration, Instant},
};

fn version_info(image: &[u8]) -> Option<Vec<u8>> {
    let u16_at = |at: usize| Some(u16::from_le_bytes(image.get(at..at + 2)?.try_into().ok()?));
    let u32_at = |at: usize| Some(u32::from_le_bytes(image.get(at..at + 4)?.try_into().ok()?));
    let pe = u32_at(0x3c)? as usize;
    let sections = u16_at(pe + 6)? as usize;
    let optional = pe + 24;
    let optional_size = u16_at(pe + 20)? as usize;
    let resources = u32_at(optional + 96 + 2 * 8)?;
    let table = optional + optional_size;
    let offset_of = |rva: u32| -> Option<usize> {
        (0..sections).find_map(|i| {
            let s = table + 40 * i;
            let (size, at, raw) = (u32_at(s + 8)?, u32_at(s + 12)?, u32_at(s + 20)?);
            (rva >= at && rva < at + size.max(u32_at(s + 16)?)).then_some((rva - at + raw) as usize)
        })
    };
    let base = offset_of(resources)?;
    let entry = |directory: usize, id: Option<u32>| -> Option<(bool, usize)> {
        let count = u16_at(directory + 12)? as usize + u16_at(directory + 14)? as usize;
        (0..count).find_map(|i| {
            let e = directory + 16 + 8 * i;
            let (name, target) = (u32_at(e)?, u32_at(e + 4)?);
            (id.is_none_or(|id| name == id)).then_some((
                target & 0x8000_0000 != 0,
                base + (target & 0x7fff_ffff) as usize,
            ))
        })
    };
    let (true, names) = entry(base, Some(16))? else {
        return None;
    };
    let (true, languages) = entry(names, None)? else {
        return None;
    };
    let (false, data) = entry(languages, None)? else {
        return None;
    };
    let at = offset_of(u32_at(data)?)?;
    Some(image.get(at..at + u32_at(data + 4)? as usize)?.to_vec())
}

/// Shell-like split of the flags file (double quotes group).
fn split_flags(text: &str) -> Vec<String> {
    let mut out = vec![];
    let mut cur = String::new();
    let mut quoted = false;
    let mut any = false;
    for c in text.chars() {
        match c {
            '"' => {
                quoted = !quoted;
                any = true;
            }
            c if c.is_whitespace() && !quoted => {
                if any {
                    out.push(std::mem::take(&mut cur));
                    any = false;
                }
            }
            c => {
                cur.push(c);
                any = true;
            }
        }
    }
    if any {
        out.push(cur);
    }
    out
}

/// Top-level chunks of BODY: cut where the brace depth is 0 after a `}` or `;` that ends a line.
fn chunks(body: &str) -> Vec<String> {
    let mut out = vec![];
    let mut cur = String::new();
    let mut depth = 0i32;
    for line in body.lines() {
        cur.push_str(line);
        cur.push('\n');
        let code = line.split("//").next().unwrap_or("");
        for c in code.chars() {
            match c {
                '{' => depth += 1,
                '}' => depth -= 1,
                _ => {}
            }
        }
        let t = code.trim_end();
        if depth == 0 && (t.ends_with('}') || t.ends_with(';')) && !cur.trim().is_empty() {
            out.push(std::mem::take(&mut cur));
        }
    }
    if !cur.trim().is_empty() {
        out.push(cur);
    }
    out
}

/// mwdec's persist-bench `--mixed` body K (crates/mwdec-oracle/src/bin/persist-bench.rs).
fn mixed_body(k: usize) -> String {
    match k % 6 {
        0 => format!(
            "float M{k}(const CVector3f& a, const CVector3f& b) {{ CVector3f d = a - b * {k}.25f; return d.MagSquared() + CVector3f::Dot(a, d); }}\n"
        ),
        1 => format!(
            "const char* M{k}(int x) {{ static int cnt; cnt += x; switch (x) {{ case 0: return \"zero{k}\"; case 1: return \"one\"; case 2: return \"two\"; case 5: return \"five\"; default: return cnt > {k} ? \"big\" : \"small\"; }} }}\n"
        ),
        2 => format!(
            "struct V{k} {{ virtual ~V{k}(); virtual int Get() const {{ return {k}; }} int m; }};\nV{k}::~V{k}() {{}}\nint M{k}(V{k}* v) {{ return v->Get() + v->m; }}\n"
        ),
        3 => format!(
            "template <typename T> T Tm{k}(T a, T b) {{ return a > b ? a - b : b - a; }}\nfloat M{k}(float x, int y) {{ return Tm{k}(x, {k}.5f) + (float)Tm{k}(y, {k}); }}\n"
        ),
        4 => format!(
            "static double tab{k}[3] = {{ 1.0, {k}.0, 3.5 }};\ndouble M{k}(int i, PS* s) {{ double r = tab{k}[i % 3]; for (int j = 0; j < s->a; j++) r += tab{k}[j & 1] * j; return r; }}\n"
        ),
        _ => format!(
            "int F{k}(PS* s, int x) {{ int r = pget({k}) + s->a; if (x > {k}) r += s->b; pg = r; return r ^ x; }}\n"
        ),
    }
}

struct Normal {
    exit_code: i32,
    object: Option<Vec<u8>>,
    console: Vec<u8>,
    time: Duration,
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let win = |s: &str| PathBuf::from(s.replace('/', "\\"));
    let modules = win(&a[1]);
    let exe = win(&a[2]);
    let root = win(&a[3]);
    let source = win(&a[4]);
    let flags = split_flags(&std::fs::read_to_string(&a[5]).unwrap());
    let work = win(&a[6]);
    let rounds: usize = a.get(7).map_or(1, |r| r.parse().unwrap());
    let only: Option<usize> = std::env::var("ONLY").ok().map(|v| v.parse().unwrap());
    let version_info = version_info(&std::fs::read(&exe).unwrap()).expect("version info");
    let program = exe.to_string_lossy().into_owned();
    let environment: Vec<(String, String)> = vec![];

    let text = std::fs::read_to_string(root.join(&source))
        .unwrap()
        .replace("\r\n", "\n");
    let last_include = text
        .lines()
        .enumerate()
        .filter(|(_, l)| l.starts_with("#include"))
        .map(|(i, _)| i)
        .last()
        .unwrap();
    let lines: Vec<&str> = text.lines().collect();
    let context = lines[..=last_include].join("\n") + "\n";
    let body = lines[last_include + 1..].join("\n") + "\n";
    let parts = chunks(&body);

    let mut candidates: Vec<(String, String)> = vec![("whole body".into(), body.clone())];
    for (i, _) in parts.iter().enumerate() {
        let without: String = parts
            .iter()
            .enumerate()
            .filter(|(j, _)| *j != i)
            .map(|(_, p)| p.as_str())
            .collect();
        candidates.push((format!("without chunk {i}"), without));
    }
    for (i, p) in parts.iter().enumerate() {
        candidates.push((format!("chunk {i} alone"), p.clone()));
    }
    candidates.push(("syntax error".into(), "void f() { int x = ; }\n".into()));
    candidates.push((
        "pragma opt 0 + body".into(),
        format!("#pragma optimization_level 0\n{body}"),
    ));
    candidates.push((
        "static init".into(),
        "struct S { S(); int x; }; S s; static float g = 1.5f; float h() { return g; }\n".into(),
    ));
    candidates.push(("empty".into(), String::new()));
    let big: String = (0..300)
        .map(|i| format!("int big{i}(int a) {{ int t[64]; for (int k = 0; k < 64; k++) t[k] = a * k + {i}; return t[a & 63]; }}\n"))
        .collect();
    candidates.push(("many functions".into(), big));
    candidates.push(("whole body again".into(), body.clone()));
    // MIXED=N: mwdec's persist-bench --mixed instead (its context: CStateManager.hpp and CActor.hpp, its prelude, and
    // N bodies that use header inlines, statics, strings, switches, virtual classes and templates)
    let mut context = context;
    if let Some(n) = std::env::var("MIXED")
        .ok()
        .map(|v| v.parse::<usize>().unwrap())
    {
        context = "#include \"Echoes/MetroidPrime/CStateManager.hpp\"\n#include \"Echoes/MetroidPrime/CActor.hpp\"\n\
                   struct PS { int a; int b; float f; };\nint pget(int); float pgetf(int); extern int pg;\n\
                   #include \"Echoes/Kyoto/Math/CVector3f.hpp\"\n"
            .into();
        candidates = (0..n)
            .map(|k| (format!("mixed {k}"), mixed_body(k)))
            .collect();
    }
    // CONTEXT_TAIL: appended to the context (a context that ends in a function body, a class, ...)
    if let Ok(tail) = std::env::var("CONTEXT_TAIL") {
        context.push_str(&tail.replace("\\n", "\n"));
        context.push('\n');
    }
    let capacity = candidates.iter().map(|c| c.1.len()).max().unwrap() + 4096;
    eprintln!(
        "context {} bytes, body {} bytes in {} chunks; {} candidates; slot {} bytes",
        context.len(),
        body.len(),
        parts.len(),
        candidates.len(),
        capacity
    );

    let name = source.file_name().unwrap().to_owned();
    let pdir = work.join("p");
    let rdir = work.join("r");
    std::fs::create_dir_all(&pdir).unwrap();
    std::fs::create_dir_all(&rdir).unwrap();
    let persist_src = pdir.join(&name);
    let persistent_text = persistent_source(&context, capacity);
    std::fs::write(&persist_src, &persistent_text).unwrap();
    let marker_end = persistent_text.len() - capacity;

    let args = |src: &Path, obj: &Path| -> Vec<String> {
        let mut v = flags.clone();
        v.extend([
            "-c".into(),
            src.to_string_lossy().into_owned(),
            "-o".into(),
            obj.to_string_lossy().into_owned(),
        ]);
        v
    };

    let t = Instant::now();
    let stock = Compiler::new(&std::fs::read(modules.join("mwcceppc.wasm")).unwrap()).unwrap();
    // (PERSIST_MODULE: another module of MODULES_DIR, mwcc.wasm)
    let persist_module = std::env::var("PERSIST_MODULE").unwrap_or_else(|_| "persist.wasm".into());
    let persist = Compiler::new(&std::fs::read(modules.join(persist_module)).unwrap()).unwrap();
    eprintln!("modules compiled in {:?}", t.elapsed());

    // PLAIN=1: the reference is a compile of the context and the candidate alone (no sentinel, marker or padding), as
    // the project builds it
    let plain = std::env::var_os("PLAIN").is_some();
    let normal = |candidate: &str| -> Normal {
        let mut text;
        if plain {
            text = context.clone().into_bytes();
            text.extend(candidate.replace("\r\n", "\n").bytes());
        } else {
            text = persistent_text[..marker_end].to_vec();
            text.extend(slot_text(candidate.as_bytes(), capacity).unwrap());
        }
        let src = rdir.join(&name);
        let obj = rdir.join("out.o");
        std::fs::write(&src, &text).unwrap();
        // (the persistent source's modification time: `-sym on` objects record it)
        if let Ok(time) = std::fs::metadata(&persist_src).and_then(|m| m.modified()) {
            let _ = std::fs::File::options()
                .write(true)
                .open(&src)
                .and_then(|f| f.set_modified(time));
        }
        let _ = std::fs::remove_file(&obj);
        let a = args(&src, &obj);
        let t = Instant::now();
        let o = stock
            .run(&Request {
                args: &a,
                directory: &root,
                environment: &environment,
                program: &program,
                version_info: &version_info,
                timeout: Some(Duration::from_secs(120)),
                cancel: None,
                trace: false,
                overlay: None,
                transcode_sjis: false,
                record_reads: false,
                command_line: None,
            })
            .unwrap();
        let time = t.elapsed();
        Normal {
            exit_code: o.exit_code,
            object: if o.exit_code == 0 {
                std::fs::read(&obj).ok()
            } else {
                None
            },
            console: o.console,
            time,
        }
    };

    let pa = args(&persist_src, &pdir.join("out.o"));
    let t = Instant::now();
    let mut p = persist
        .persistent(
            &Request {
                args: &pa,
                directory: &root,
                environment: &environment,
                program: &program,
                version_info: &version_info,
                timeout: Some(Duration::from_secs(120)),
                cancel: None,
                trace: false,
                overlay: None,
                transcode_sjis: false,
                record_reads: false,
                command_line: None,
            },
            capacity,
        )
        .unwrap_or_else(|e| panic!("start: {e:?}"));
    eprintln!(
        "persistent start {:?} (snapshot {} MB)",
        t.elapsed(),
        p.snapshot_bytes >> 20
    );

    // TIMEOUT_TEST=1: the first candidate with a 5 ms timeout ends the run; a new one starts and compiles
    if std::env::var_os("TIMEOUT_TEST").is_some() {
        let t = Instant::now();
        let r = p.compile(candidates[0].1.as_bytes(), Some(Duration::from_millis(5)));
        eprintln!(
            "timeout test: {} after {:?}, alive {}",
            match &r {
                Ok(c) => format!("Ok (exit {})", c.exit_code),
                Err(e) => format!("Err ({})", e.to_string().lines().next().unwrap_or("")),
            },
            t.elapsed(),
            p.alive()
        );
        let t = Instant::now();
        drop(p);
        eprintln!("dropped in {:?}", t.elapsed());
        p = persist
            .persistent(
                &Request {
                    args: &pa,
                    directory: &root,
                    environment: &environment,
                    program: &program,
                    version_info: &version_info,
                    timeout: Some(Duration::from_secs(120)),
                    cancel: None,
                    trace: false,
                    overlay: None,
                    transcode_sjis: false,
                    record_reads: false,
                    command_line: None,
                },
                capacity,
            )
            .unwrap_or_else(|e| panic!("restart: {e:?}"));
        eprintln!("restarted");
    }

    let (mut same, mut differ) = (0, 0);
    let (mut p_total, mut n_total, mut restore_total, mut compile_total) = (
        Duration::ZERO,
        Duration::ZERO,
        Duration::ZERO,
        Duration::ZERO,
    );
    let mut count = 0;
    let mut references: Vec<Option<Normal>> = (0..candidates.len()).map(|_| None).collect();
    for round in 0..rounds {
        for (i, (label, candidate)) in candidates.iter().enumerate() {
            if only.is_some_and(|o| o != i) {
                continue;
            }
            let t = Instant::now();
            // (TRACE_EVERY=N: every Nth candidate traced, which must not change its object)
            let traced = std::env::var("TRACE_EVERY")
                .ok()
                .and_then(|n| n.parse::<usize>().ok())
                .is_some_and(|n| i % n == 0);
            let r = match p.compile_traced(
                candidate.as_bytes(),
                Some(Duration::from_secs(120)),
                traced,
            ) {
                Ok(r) => r,
                Err(e) => {
                    eprintln!("[{i}] {label}: persistent compile failed: {e:?}");
                    differ += 1;
                    break;
                }
            };
            let pt = t.elapsed();
            if traced {
                // (empty for a candidate without code)
                eprintln!("[{i}] traced: {} bytes of trace", r.trace.len());
            } else {
                assert!(r.trace.is_empty(), "an untraced candidate has a trace");
            }
            if references[i].is_none() {
                references[i] = Some(normal(candidate));
            }
            let n = references[i].as_ref().unwrap();
            let mask = |c: &[u8], dir: &Path| {
                String::from_utf8_lossy(c).replace(&*dir.to_string_lossy(), "<dir>")
            };
            let p_msgs = mask(&r.console, &pdir);
            let n_msgs = mask(&n.console, &rdir);
            let msgs_ok = n_msgs.trim_end().ends_with(p_msgs.trim_end());
            if !msgs_ok {
                eprintln!("[{i}] messages differ:\n--- persist\n{p_msgs}\n--- normal\n{n_msgs}");
            }
            let ok = n.exit_code == r.exit_code && n.object == r.object && msgs_ok;
            if ok {
                same += 1;
            } else {
                differ += 1;
            }
            p_total += pt;
            n_total += n.time;
            restore_total += r.restore;
            compile_total += r.compile;
            count += 1;
            if round == 0 || !ok {
                eprintln!(
                    "[{i:2}] {label:24} {} exit {}/{} obj {:>7}/{:>7} B | persist {:6.1} ms (restore {:4.1}, compile {:6.1}) normal {:6.1} ms{}{}",
                    if ok { "same  " } else { "DIFFER" },
                    r.exit_code,
                    n.exit_code,
                    r.object.as_ref().map_or(0, Vec::len),
                    n.object.as_ref().map_or(0, Vec::len),
                    pt.as_secs_f64() * 1e3,
                    r.restore.as_secs_f64() * 1e3,
                    r.compile.as_secs_f64() * 1e3,
                    n.time.as_secs_f64() * 1e3,
                    if r.zeroed > 0 {
                        format!(" [restore zeroed {} KB above the snapshot]", r.zeroed >> 10)
                    } else {
                        String::new()
                    },
                    if r.console.is_empty() {
                        String::new()
                    } else {
                        format!(
                            "\n      persist console: {:?}\n      normal console:  {:?}",
                            String::from_utf8_lossy(&r.console)
                                .chars()
                                .take(300)
                                .collect::<String>(),
                            String::from_utf8_lossy(&n.console)
                                .chars()
                                .take(300)
                                .collect::<String>()
                        )
                    }
                );
            }
            if !ok && let (Some(a), Some(b)) = (&r.object, &n.object) {
                std::fs::write(work.join(format!("diff{i}.persist.o")), a).unwrap();
                std::fs::write(work.join(format!("diff{i}.normal.o")), b).unwrap();
            }
        }
    }
    let avg = |d: Duration| d.as_secs_f64() * 1e3 / count.max(1) as f64;
    eprintln!(
        "{same} same, {differ} differ; per candidate: persistent {:.1} ms (restore {:.1}, compile {:.1}), normal {:.1} ms",
        avg(p_total),
        avg(restore_total),
        avg(compile_total),
        avg(n_total)
    );
}

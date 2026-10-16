//! Internal benchmark worker; not a production API. See tools/migration/benchmark.py.
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    allocation, colouring,
    colouring::search::{self, Options, Problem},
    document, runner, trace,
    version::{self, Version},
};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    env, fs,
    hint::black_box,
    path::PathBuf,
    time::{Duration, Instant},
};

fn summary(parsed: &trace::Trace) -> Value {
    json!(parsed.allocations.iter().map(|a| json!({
        "link":a.before.link,"before_blocks":a.before.blocks.len(),"after_blocks":a.after.blocks.len(),
        "rounds":a.before.rounds.iter().map(|r|json!([r.class,r.nodes.len(),r.order.len()])).collect::<Vec<_>>()
    })).collect::<Vec<_>>())
}

fn compile(input: &Value, analyze: bool) -> Result<Value> {
    let c = &input["compile"];
    let version = Version::parse(input["version"].as_str().context("version")?)?;
    let compiled = runner::compile(&runner::CompileRequest {
        root: PathBuf::from(c["root"].as_str().context("compile root")?),
        working_directory: PathBuf::from(
            c["working_directory"]
                .as_str()
                .context("working directory")?,
        ),
        source: c["source"].as_str().context("source")?.into(),
        flags: serde_json::from_value(c["flags"].clone())?,
        version,
        options: runner::TraceOptions::default(),
        overrides: vec![],
        check: true,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(600),
    })?;
    ensure!(
        compiled.check_status == runner::CheckStatus::Verified,
        "benchmark requires verified compilation"
    );
    if analyze {
        let parsed = trace::parse(&compiled.trace)?;
        document::build(
            &parsed,
            version,
            &compiled.source,
            &compiled.oracle,
            compiled.identical,
            &compiled.differences,
            &document::Selection::default(),
        )
    } else {
        Ok(
            json!({"object_sha256":compiled.object_sha256,"verified":true,
                  "trace_lines":compiled.trace.lines().count(),"trace_present":compiled.trace_present}),
        )
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().skip(1).collect();
    ensure!(
        args.len() == 5,
        "migration_bench INPUT PHASE SAMPLES BATCH RESULT"
    );
    let input: Value = serde_json::from_slice(&fs::read(&args[0])?)?;
    let phase = &args[1];
    let samples: usize = args[2].parse()?;
    let mut batch: usize = args[3].parse()?;
    ensure!(samples > 0, "positive samples required");
    let version = Version::parse(input["version"].as_str().context("version")?)?;
    let text = if phase == "compile_checked" || phase == "end_to_end_checked" {
        String::new()
    } else {
        fs::read_to_string(input["trace"].as_str().context("trace")?)?
    };
    let parsed = if phase == "parse" || text.is_empty() {
        trace::Trace::default()
    } else {
        trace::parse(&text)?
    };
    let rounds: Vec<_> = parsed
        .allocations
        .iter()
        .flat_map(|a| {
            ["GPR", "FPR"].into_iter().filter_map(move |class| {
                let r: Vec<_> = a
                    .before
                    .rounds
                    .iter()
                    .filter(|r| version::class_name(r.class) == class)
                    .collect();
                r.last().map(|last| (*last, class, r[0].count))
            })
        })
        .collect();
    let search_input = &input["search"];
    let round: trace::Round = serde_json::from_value(search_input["round"].clone())?;
    let targets: BTreeMap<u32, i32> = serde_json::from_value(search_input["targets"].clone())?;
    let class = search_input["class"].as_str().context("class")?;
    let count = search_input["count"].as_u64().map(|n| n as u32);
    let problem = Problem::new(&round, class, count, targets)?;
    let options = Options {
        kinds: serde_json::from_value(search_input["options"]["kinds"].clone())?,
        iterations: search_input["options"]["iterations"]
            .as_u64()
            .context("iterations")? as usize,
        seed: search_input["options"]["seed"]
            .as_str()
            .context("seed")?
            .into(),
        restarts: search_input["options"]["restarts"]
            .as_u64()
            .context("restarts")? as usize,
        nodes: serde_json::from_value(search_input["options"]["nodes"].clone())?,
        ..Default::default()
    };
    // Keep result serialization outside the timer; parsing's summary is also
    // outside it. Every timed result is consumed and destroyed before stopping.
    let run = || -> Result<()> {
        match phase.as_str() {
            "parse" => drop(black_box(trace::parse(black_box(&text))?)),
            "analyze" => drop(black_box(
                parsed
                    .allocations
                    .iter()
                    .map(|a| allocation::analyze(a, version))
                    .collect::<Vec<_>>(),
            )),
            "replay" => drop(black_box(
                rounds
                    .iter()
                    .map(|(r, c, k)| colouring::replay(r, c, Some(*k), &[], None))
                    .collect::<Result<Vec<_>>>()?,
            )),
            "search" => drop(black_box(search::search(&problem, &options)?)),
            "compile_checked" => drop(black_box(compile(&input, false)?)),
            "end_to_end_checked" => drop(black_box(compile(&input, true)?)),
            _ => bail!("unknown phase {phase}"),
        }
        Ok(())
    };
    let warmup = Instant::now();
    run()?; // one explicit warm-up; optionally calibrate to ~100ms per sample
    if batch == 0 {
        batch =
            (100_000_000_u128.div_ceil(warmup.elapsed().as_nanos().max(1))).clamp(1, 1000) as usize;
    }
    let mut durations = vec![];
    for _ in 0..samples {
        let began = Instant::now();
        for _ in 0..batch {
            run()?;
        }
        durations.push(began.elapsed().as_nanos() as u64);
    }
    let result = match phase.as_str() {
        "parse" => summary(&trace::parse(&text)?),
        "analyze" => json!(
            parsed
                .allocations
                .iter()
                .map(|a| allocation::analyze(a, version))
                .collect::<Vec<_>>()
        ),
        "replay" => json!(
            rounds
                .iter()
                .map(|(r, c, k)| colouring::replay(r, c, Some(*k), &[], None))
                .collect::<Result<Vec<_>>>()?
        ),
        "search" => serde_json::to_value(search::search(&problem, &options)?)?,
        "compile_checked" => compile(&input, false)?,
        "end_to_end_checked" => compile(&input, true)?,
        _ => unreachable!(),
    };
    fs::write(&args[4], serde_json::to_vec(&result)?)?;
    println!(
        "{}",
        json!({"phase":phase,"samples_ns":durations,"batch":batch,"warmups":1})
    );
    Ok(())
}

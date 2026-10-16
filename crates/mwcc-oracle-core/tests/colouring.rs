use mwcc_oracle_core::{
    colouring::search::{self, Options, Problem},
    python_random::Random,
    trace::Round,
};
use serde_json::Value;
use std::{collections::BTreeMap, fs, path::PathBuf};
fn fixture(name: &str) -> Value {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/colouring")
        .join(name);
    serde_json::from_slice(&fs::read(path).unwrap()).unwrap()
}

#[test]
fn mixed_random_operations_match_cpython_across_twists() {
    for case in fixture("random.json").as_array().unwrap() {
        let seed = case["seed"].as_str().unwrap();
        let mut random = Random::new(seed).unwrap();
        for operation in case["operations"].as_array().unwrap() {
            assert_eq!(
                random.random().to_bits(),
                operation["random_bits"].as_u64().unwrap(),
                "seed {seed}"
            );
            for (n, expected) in [1, 2, 3, 16, 29, 65537]
                .into_iter()
                .zip(operation["below"].as_array().unwrap())
            {
                assert_eq!(random.below(n).unwrap() as u64, expected.as_u64().unwrap());
            }
            for (n, key) in [(21, "sample_small"), (23, "sample_large")] {
                assert_eq!(
                    serde_json::to_value(random.sample_two(n).unwrap()).unwrap(),
                    operation[key]
                );
            }
            for (width, expected) in [0, 1, 31, 32, 33, 64]
                .into_iter()
                .zip(operation["bits"].as_array().unwrap())
            {
                assert_eq!(random.bits(width).unwrap(), expected.as_u64().unwrap());
            }
        }
    }
}

#[test]
fn seeded_search_force_and_tightening_match_python() {
    for case in fixture("search.json").as_array().unwrap() {
        let name = case["name"].as_str().unwrap();
        let round: Round = serde_json::from_value(case["round"].clone()).unwrap();
        let targets: BTreeMap<u32, i32> = serde_json::from_value(case["targets"].clone()).unwrap();
        let problem = Problem::new(&round, "GPR", None, targets).unwrap();
        assert_eq!(
            serde_json::to_value(search::force(&problem).unwrap()).unwrap(),
            case["force"],
            "force {name}"
        );
        let o = &case["options"];
        let options = Options {
            kinds: serde_json::from_value(o["kinds"].clone()).unwrap(),
            iterations: o["iterations"].as_u64().unwrap() as usize,
            seed: o["seed"].as_str().unwrap().into(),
            restarts: o["restarts"].as_u64().unwrap() as usize,
            ..Default::default()
        };
        assert_eq!(
            serde_json::to_value(search::search(&problem, &options).unwrap()).unwrap(),
            case["search"],
            "search {name}"
        );
    }
}

#[test]
fn decoder_and_instruction_masks_match_python() {
    use mwcc_oracle_core::{colouring::target, ppc};
    for case in fixture("decoder.json").as_array().unwrap() {
        let word = case["word"].as_u64().unwrap() as u32;
        assert_eq!(
            serde_json::to_value(target::register_fields(word)).unwrap(),
            case["fields"],
            "fields {word:08x}"
        );
        assert_eq!(
            target::shape(word) as u64,
            case["shape"].as_u64().unwrap(),
            "shape {word:08x}"
        );
        assert_eq!(
            ppc::instruction_token(word) as u64,
            case["token"].as_u64().unwrap(),
            "token {word:08x}"
        );
        assert_eq!(
            serde_json::to_value(ppc::r1_offset(word)).unwrap(),
            case["r1_offset"],
            "offset {word:08x}"
        );
    }
}

fn saved(id: &str) -> (mwcc_oracle_core::trace::Trace, Vec<u8>) {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/oracle");
    (
        mwcc_oracle_core::trace::parse(
            &fs::read_to_string(root.join(format!("{id}.trace"))).unwrap(),
        )
        .unwrap(),
        fs::read(root.join(format!("{id}.o"))).unwrap(),
    )
}

#[test]
fn target_pairing_and_conflicts_match_python() {
    use mwcc_oracle_core::{colouring::target, ppc};
    let mut cache = BTreeMap::new();
    for case in fixture("targets.json").as_array().unwrap() {
        let id = case["case"].as_str().unwrap();
        let (trace, object) = cache.entry(id.to_owned()).or_insert_with(|| saved(id));
        let index = case["allocation"].as_u64().unwrap() as usize;
        let allocation = &trace.allocations[index];
        let words = ppc::function_words(object, &allocation.before.link)
            .unwrap()
            .unwrap();
        let wants = serde_json::from_value(case["wants"].clone()).unwrap();
        let theirs: Vec<u32> = serde_json::from_value(case["words"].clone()).unwrap();
        let actual = target::derive(
            allocation,
            &words,
            &theirs,
            case["class"].as_str().unwrap(),
            &wants,
        )
        .unwrap();
        assert_eq!(
            serde_json::to_value(actual).unwrap(),
            case["expected"],
            "{id} index {index} {} {}",
            case["class"],
            case["variant"]
        );
    }
}

#[test]
fn remapping_and_graph_changes_match_python() {
    use mwcc_oracle_core::{colouring::target, version::Version};
    let mut cache = BTreeMap::new();
    for case in fixture("remaps.json").as_array().unwrap() {
        let first = case["first"].as_str().unwrap();
        let second = case["second"].as_str().unwrap();
        let index = case["allocation"].as_u64().unwrap() as usize;
        cache
            .entry(first.to_owned())
            .or_insert_with(|| saved(first));
        cache
            .entry(second.to_owned())
            .or_insert_with(|| saved(second));
        let a = &cache[first].0.allocations[index];
        let b = &cache[second].0.allocations[index];
        let actual = target::remap(a, b, Version::Gc27)
            .json(a, b, case["class"].as_str().unwrap())
            .unwrap();
        assert_eq!(actual, case["expected"], "{first}/{second}/{index}");
    }
}

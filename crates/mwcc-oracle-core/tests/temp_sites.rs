//! The WebAssembly oracle's table of the temporaries' creators (crates/mwcc-oracle-hooks/src/temp_sites.rs) against
//! gc2's sources and, where they are downloaded, the original executables.
#[allow(dead_code)]
#[path = "../../mwcc-oracle-hooks/src/temp_sites.rs"]
mod temp_sites;

use regex::Regex;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
};
use temp_sites::{INLINED, NONE, SITES};

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// Each call of FUNCTION in gc2's sources: its unit and the number of the macro it is wrapped in (MACRO), or None.
fn calls(function: &str, wrapper: &str) -> Vec<(String, u32, Option<u8>)> {
    let call = Regex::new(&format!(r"\b{function}\(")).unwrap();
    let wrapped = Regex::new(&format!(r"\b{wrapper}\((\d+), $")).unwrap();
    let mut out = vec![];
    let mut units = vec![];
    for directory in ["src/backend", "src/frontend", "src/optimizer"] {
        for entry in fs::read_dir(root().join("gc2").join(directory)).unwrap() {
            let path = entry.unwrap().path();
            if path.extension().is_some_and(|e| e == "c") {
                units.push(format!(
                    "{directory}/{}",
                    path.file_name().unwrap().to_string_lossy()
                ));
            }
        }
    }
    units.sort();
    for unit in units {
        let text = fs::read_to_string(root().join("gc2").join(&unit)).unwrap();
        for (index, line) in text.lines().enumerate() {
            for found in call.find_iter(line) {
                let before = &line[..found.start()];
                // (the declarations and the definition)
                if before.trim_end().ends_with('*')
                    || before.trim().starts_with("static")
                    || before.is_empty()
                {
                    continue;
                }
                let site = wrapped
                    .captures(before)
                    .map(|c| c[1].parse::<u8>().unwrap());
                out.push((unit.clone(), index as u32 + 1, site));
            }
        }
    }
    out
}

#[test]
fn every_call_of_create_temp_object_has_its_site() {
    let rows: BTreeMap<u8, BTreeSet<&str>> = SITES.iter().fold(BTreeMap::new(), |mut m, row| {
        m.entry(row.0).or_default().insert(row.2);
        m
    });
    let mut seen = BTreeSet::new();
    for (unit, line, site) in calls("create_temp_object", "ORACLE_TEMP") {
        let site =
            site.unwrap_or_else(|| panic!("{unit}:{line}: create_temp_object without ORACLE_TEMP"));
        assert!(seen.insert(site), "{unit}:{line}: site {site} twice");
        assert!(
            rows.get(&site)
                .is_some_and(|units| units.len() == 1 && units.contains(unit.as_str())),
            "{unit}:{line}: site {site} is not this unit's in temp_sites.rs"
        );
    }
    assert_eq!(
        seen,
        rows.keys().copied().collect(),
        "temp_sites.rs's sites"
    );
}

#[test]
fn every_call_of_an_inlined_function_with_sites_has_its_copy() {
    let mut copies: BTreeMap<u8, BTreeSet<u8>> = BTreeMap::new();
    for row in SITES.iter().filter(|row| row.1 != 0) {
        copies.entry(row.1).or_default().insert(row.0);
    }
    let mut seen = BTreeSet::new();
    for (function, sites) in [
        ("CExcept_GetETEMPObject", [5, 6].as_slice()),
        ("CInline_LoadToTemp", &[16]),
    ] {
        for (unit, line, copy) in calls(function, "ORACLE_TEMP_INLINED") {
            let copy = copy
                .unwrap_or_else(|| panic!("{unit}:{line}: {function} without ORACLE_TEMP_INLINED"));
            assert!(seen.insert(copy), "{unit}:{line}: copy {copy} twice");
            assert_eq!(
                copies
                    .get(&copy)
                    .map(|s| s.iter().copied().collect::<Vec<_>>()),
                Some(sites.to_vec()),
                "{unit}:{line}: copy {copy}'s sites"
            );
        }
    }
    assert_eq!(
        seen,
        copies.keys().copied().collect(),
        "temp_sites.rs's copies"
    );
}

/// The return addresses of the calls of create_temp_object (`call rel32`) in the executable at PATH, or None
/// without it.
fn original_calls(version: &str, directory: &str) -> Option<BTreeSet<u32>> {
    let data = fs::read(
        root()
            .join("build/compilers/GC")
            .join(directory)
            .join("mwcceppc.exe"),
    )
    .ok()?;
    let symbols =
        fs::read_to_string(root().join("config").join(version).join("symbols.txt")).unwrap();
    let target = symbols
        .lines()
        .find_map(|l| l.strip_prefix("_create_temp_object = .text:0x"))
        .map(|rest| u32::from_str_radix(&rest[..8], 16).unwrap())
        .unwrap();
    let word = |at: usize| u32::from_le_bytes(data[at..at + 4].try_into().unwrap());
    let header = word(0x3c) as usize;
    let sections = u16::from_le_bytes([data[header + 6], data[header + 7]]) as usize;
    let optional = u16::from_le_bytes([data[header + 20], data[header + 21]]) as usize;
    let base = word(header + 52);
    let mut out = BTreeSet::new();
    for index in 0..sections {
        let section = header + 24 + optional + 40 * index;
        if &data[section..section + 5] != b".text" {
            continue;
        }
        let (address, size, offset) = (
            base + word(section + 12),
            word(section + 16) as usize,
            word(section + 20) as usize,
        );
        let text = &data[offset..offset + size];
        for at in 0..size - 5 {
            let relative = i32::from_le_bytes(text[at + 1..at + 5].try_into().unwrap());
            let after = address + at as u32 + 5;
            if text[at] == 0xe8 && after.wrapping_add_signed(relative) == target {
                out.insert(after);
            }
        }
    }
    Some(out)
}

#[test]
fn the_sites_are_the_originals_calls() {
    for (version, directory, column) in [
        ("GC_2_7", "2.7", 3),
        ("GC_2_6", "2.6", 4),
        ("GC_1_3_2", "1.3.2", 5),
    ] {
        let Some(calls) = original_calls(version, directory) else {
            eprintln!("skipped {version}: no build/compilers/GC/{directory}/mwcceppc.exe");
            continue;
        };
        let sites: BTreeSet<u32> = SITES
            .iter()
            .map(|row| [row.3, row.4, row.5][column - 3])
            .filter(|&site| site != NONE && site != INLINED)
            .collect();
        assert_eq!(sites, calls, "{version}'s calls of create_temp_object");
    }
}

#[test]
fn the_cse_view_names_each_sites_creator() {
    use mwcc_oracle_core::{cse::temp_creator, version::Version};
    for (version, column) in [(Version::Gc27, 3), (Version::Gc26, 4), (Version::Gc132, 5)] {
        for row in SITES.iter() {
            let site = [row.3, row.4, row.5][column - 3];
            if site == NONE || site == INLINED {
                continue;
            }
            let creator = temp_creator(version, site);
            assert!(
                !creator.starts_with("0x") && !creator.starts_with("an unknown"),
                "{version:?} site {} ({:#x}): {creator}",
                row.0,
                site
            );
        }
    }
    assert_eq!(
        temp_creator(Version::Gc132, 0x460b28).split(' ').next(),
        temp_creator(Version::Gc27, 0x461748).split(' ').next()
    );
}

#[test]
fn a_site_is_looked_up_in_its_copy() {
    assert_eq!(temp_sites::site(5, 2, 3), 0x5208d9);
    assert_eq!(temp_sites::site(16, 11, 2), 0x55e523);
    assert_eq!(temp_sites::site(16, 0, 3), NONE);
    // (a site outside the copies, with a copy left behind)
    assert_eq!(temp_sites::site(42, 7, 2), 0x460b28);
    assert_eq!(temp_sites::site(42, 0, 5), NONE);
}

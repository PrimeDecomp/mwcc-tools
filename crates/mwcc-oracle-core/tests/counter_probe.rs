use mwcc_oracle_core::{coff::Object, counter_probe, executable};
use std::{
    collections::BTreeMap,
    env, fs,
    path::{Path, PathBuf},
};
fn collect(root: &Path, objects: &mut Vec<(String, Vec<u8>)>) {
    for item in fs::read_dir(root).unwrap() {
        let path = item.unwrap().path();
        if path.is_dir() {
            collect(&path, objects);
        } else if path.extension().is_some_and(|v| v == "obj") {
            objects.push((path.to_string_lossy().into(), fs::read(path).unwrap()));
        }
    }
}
#[test]
#[ignore = "requires split originals and a counter-instrumented producer in MWCC_ORACLE_TEST_ROOT"]
fn counter_islands_preserve_all_unpatched_bytes_symbols_sizes_and_guard_sites() {
    let root =
        PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set MWCC_ORACLE_TEST_ROOT"));
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let mut inputs = vec![];
        collect(&root.join(format!("build/{version}/obj")), &mut inputs);
        let splits = executable::read_splits(
            &fs::read_to_string(root.join(format!("config/{version}/splits.txt"))).unwrap(),
        )
        .unwrap();
        let order: BTreeMap<_, _> = splits
            .units
            .iter()
            .enumerate()
            .map(|(i, u)| (u.name.as_str(), i))
            .collect();
        let objdir = root.join(format!("build/{version}/obj"));
        inputs.sort_by_key(|(path, _)| {
            let relative = Path::new(path)
                .strip_prefix(&objdir)
                .unwrap()
                .to_string_lossy()
                .replace('\\', "/");
            *order
                .get(relative.strip_suffix(".obj").unwrap())
                .unwrap_or(&usize::MAX)
        });
        let installed = counter_probe::install(&root, version, &inputs).unwrap();
        assert_eq!(
            installed.islands,
            fs::read(root.join(format!(
                "build/{version}/oracle/compiled/rust/counter-islands.obj"
            )))
            .unwrap(),
            "current islands differ from the retained runtime proof"
        );
        let expected = match version {
            "GC_1_3" => 574,
            "GC_1_3_2" => 575,
            _ => 582,
        };
        assert_eq!(installed.sites.len(), expected);
        let materializers = installed
            .sites
            .iter()
            .filter(|s| s.operand_binding != counter_probe::OperandBinding::None)
            .collect::<Vec<_>>();
        assert_eq!(materializers.len(), 2);
        let origins = installed
            .sites
            .iter()
            .filter(|s| s.origin_binding != counter_probe::OriginBinding::None)
            .collect::<Vec<_>>();
        assert_eq!(origins.len(), 8);
        let scalar = origins
            .iter()
            .filter(|s| s.origin_binding == counter_probe::OriginBinding::ScalarArrayElement)
            .collect::<Vec<_>>();
        assert_eq!(
            scalar
                .iter()
                .map(|s| (s.offset, s.counter_class))
                .collect::<Vec<_>>(),
            [(0x332, Some(3)), (0x345, Some(4))]
        );
        let calls = origins
            .iter()
            .filter(|s| s.container == "_FunctionCalls_GenerateCall")
            .collect::<Vec<_>>();
        assert_eq!(
            calls
                .iter()
                .map(|s| (s.offset, s.counter_class))
                .collect::<Vec<_>>(),
            [
                (0x4c7, Some(3)),
                (0x514, Some(2)),
                (0x55d, Some(3)),
                (0x602, Some(4)),
                (0x612, Some(4)),
                (0x66d, Some(4))
            ]
        );
        assert_eq!(
            calls[3].origin_binding,
            counter_probe::OriginBinding::CallResultFirst
        );
        assert_eq!(
            calls[4].origin_binding,
            counter_probe::OriginBinding::CallResultSecond
        );
        assert_eq!(
            materializers[0].operand_binding,
            counter_probe::OperandBinding::SymbolAddressHigh
        );
        assert_eq!(
            materializers[1].operand_binding,
            counter_probe::OperandBinding::SymbolAddressLow
        );
        assert!(
            installed
                .sites
                .iter()
                .all(|s| s.data_link_offset == if version == "GC_2_7" { 54 } else { 56 })
        );
        let mut coverage = 0;
        for (path, bytes) in &inputs {
            let Some(new) = installed.objects.get(path) else {
                continue;
            };
            let old = Object::parse(bytes).unwrap();
            let new = Object::parse(new).unwrap();
            assert_eq!(old.sections.len(), new.sections.len());
            let mut allowed: BTreeMap<usize, Vec<std::ops::Range<usize>>> = BTreeMap::new();
            for site in &installed.sites {
                if let Some(s) = old
                    .symbols
                    .values()
                    .find(|s| s.section > 0 && s.name == site.container)
                {
                    allowed.entry(s.section as usize - 1).or_default().push(
                        (s.value + site.offset) as usize
                            ..(s.value + site.offset) as usize + site.bytes.len(),
                    );
                    coverage += 1;
                }
            }
            for (index, (a, b)) in old.sections.iter().zip(&new.sections).enumerate() {
                assert_eq!(a.name, b.name);
                assert_eq!(a.size, b.size);
                assert_eq!(a.flags, b.flags);
                for (offset, (a, b)) in a.data.iter().zip(b.data.iter()).enumerate() {
                    if !allowed
                        .get(&index)
                        .is_some_and(|ranges| ranges.iter().any(|r| r.contains(&offset)))
                    {
                        assert_eq!(a, b);
                    }
                }
            }
            for s in old.symbols.values().filter(|s| s.storage == 2) {
                let changed = new.symbols.values().find(|n| n.name == s.name).unwrap();
                assert_eq!((s.section, s.value), (changed.section, changed.value));
            }
        }
        assert_eq!(coverage, expected);
        for site in [&installed.sites[0], scalar[0], calls[4]] {
            let mut locations = None;
            for (index, (_, bytes)) in inputs.iter().enumerate() {
                let old = Object::parse(bytes).unwrap();
                if let Some(s) = old
                    .symbols
                    .values()
                    .find(|s| s.section > 0 && s.name == site.container)
                {
                    let start =
                        old.sections[s.section as usize - 1].raw_offset as usize + s.value as usize;
                    locations = Some((index, start, start + site.offset as usize));
                    break;
                }
            }
            let (index, start, at) = locations.unwrap();
            for position in [at, start, at + site.field.unwrap()] {
                let mut damaged = inputs.clone();
                damaged[index].1[position] ^= 1;
                assert!(counter_probe::install(&root, version, &damaged).is_err());
            }
        }
    }
}

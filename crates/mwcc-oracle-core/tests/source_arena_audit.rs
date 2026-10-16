use mwcc_oracle_core::source_arena_audit;
use std::{collections::BTreeSet, env, path::PathBuf};

#[test]
#[ignore = "requires pinned retail images in MWCC_ORACLE_TEST_ROOT"]
fn retail_audit_finds_inlined_global_allocations_and_conditional_local_resets() {
    let root = PathBuf::from(env::var_os("MWCC_ORACLE_TEST_ROOT").expect("set test root"));
    for version in ["GC_1_3", "GC_1_3_2", "GC_2_7"] {
        let audit = source_arena_audit::capture(&root, version).unwrap();
        let global = audit.arenas.iter().find(|a| a.name == "global").unwrap();
        // Wrapping galloc alone misses real cursor writes in name interning.
        let inline_writers = global
            .references
            .references
            .iter()
            .filter(|r| r.write && r.targets.contains(&(global.base + 12)))
            .map(|r| r.function.as_str())
            .collect::<BTreeSet<_>>();
        for name in [
            "_InitNameHash",
            "_GetHashNameNode",
            "_GetHashNameNodeExport",
        ] {
            assert!(inline_writers.contains(name), "{version}: {name}");
        }
        assert!(
            audit.local_lock.references.iter().any(|r| {
                r.function == "_freelheap" && r.read && !r.write && r.width == Some(2)
            })
        );
        assert!(
            audit
                .failure_callback
                .references
                .iter()
                .any(|r| { r.function == "_select_or_allocate_pool_node" && r.read })
        );
        for arena in &audit.arenas {
            assert_eq!(arena.size, 20);
            assert!(arena.storage_symbols.len() > 1);
            assert!(arena.references.unresolved.is_empty());
            assert!(arena.references.references.iter().all(|r| {
                r.function_decoded_completely
                    && r.targets
                        .iter()
                        .all(|t| (arena.base..arena.base + 20).contains(t))
            }));
        }
        assert!(!audit.allocation_coverage_complete);
        assert!(!audit.reset_coverage_complete);
    }
}

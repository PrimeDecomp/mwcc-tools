use mwcc_oracle_core::source_lifetimes::{
    Allocation, AllocationId, Coverage, CoverageLoss, GapKind, InvalidationKind, Ledger, PoolDomain,
};

fn allocation(
    id: u64,
    pool: PoolDomain,
    epoch: u64,
    address: u32,
    requested: u32,
    reserved: u32,
) -> Allocation {
    Allocation {
        id: AllocationId(id),
        pool,
        epoch,
        address,
        requested,
        reserved,
    }
}
fn started(pool: PoolDomain) -> Ledger {
    let mut ledger = Ledger::new(32);
    ledger.begin_pool(pool, 0, Coverage::COMPLETE).unwrap();
    ledger
}

#[test]
fn malformed_allocation_cannot_leave_a_colliding_other_pool_proven_live() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .begin_pool(PoolDomain::Global, 0, Coverage::COMPLETE)
        .unwrap();
    ledger
        .observe_allocation(allocation(1, PoolDomain::Global, 0, 0x1000, 8, 8))
        .unwrap();
    let before = ledger
        .resolve(PoolDomain::Global, 0, 0x1000, 8)
        .value
        .unwrap();
    assert_eq!(ledger.is_live(&before).value, Some(true));
    assert_eq!(
        ledger
            .observe_allocation(allocation(2, PoolDomain::Local, 0, 0x1000, 9, 8))
            .unwrap_err()
            .kind,
        GapKind::InvalidSpan
    );
    assert!(
        ledger
            .resolve(PoolDomain::Global, 0, 0x1000, 8)
            .value
            .is_none()
    );
    assert!(ledger.is_live(&before).value.is_none());
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    assert!(ledger.is_live(&before).value.is_none());
    ledger
        .observe_invalidation(
            PoolDomain::Global,
            Some(0),
            1,
            InvalidationKind::Release,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(3, PoolDomain::Global, 1, 0x1000, 8, 8))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Global, 1, 0x1000, 8)
            .value
            .is_some()
    );
}

#[test]
fn payload_offsets_are_distinct_from_reserved_padding_and_from_type_or_initialization() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 30, 32))
        .unwrap();
    let reference = ledger
        .resolve(PoolDomain::Local, 0, 0x1006, 4)
        .value
        .unwrap();
    assert_eq!(
        (
            reference.allocation(),
            reference.pool(),
            reference.epoch(),
            reference.offset(),
            reference.extent()
        ),
        (AllocationId(1), PoolDomain::Local, 0, 6, 4)
    );
    assert_eq!(
        ledger.resolve(PoolDomain::Local, 0, 0x101e, 1).reasons[0].kind,
        GapKind::OutsideRequestedExtent
    );
    assert_eq!(
        ledger.resolve(PoolDomain::Local, 0, 0x101c, 4).reasons[0].kind,
        GapKind::OutsideRequestedExtent
    );
    let same = ledger
        .resolve(PoolDomain::Local, 0, 0x1006, 4)
        .value
        .unwrap();
    let other = ledger
        .resolve(PoolDomain::Local, 0, 0x100a, 4)
        .value
        .unwrap();
    assert_eq!(ledger.same_allocation(&reference, &other).value, Some(true));
    assert_eq!(ledger.same_span(&reference, &same).value, Some(true));
    assert_eq!(ledger.same_span(&reference, &other).value, Some(false));
    // No node role/content/revision was supplied or inferred from requested=30.
}

#[test]
fn reset_and_release_separate_reused_addresses_but_preserve_historical_identity() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 16))
        .unwrap();
    let first = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    assert_eq!(ledger.is_live(&first).value, Some(false));
    ledger
        .observe_allocation(allocation(2, PoolDomain::Local, 1, 0x1000, 8, 16))
        .unwrap();
    let second = ledger
        .resolve(PoolDomain::Local, 1, 0x1000, 8)
        .value
        .unwrap();
    assert_eq!(ledger.same_allocation(&first, &second).value, Some(false));
    assert_eq!(ledger.same_span(&first, &first).value, Some(true));
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(1),
            2,
            InvalidationKind::Release,
            Coverage::COMPLETE,
        )
        .unwrap();
    assert_eq!(ledger.is_live(&second).value, Some(false));
    assert_eq!(
        ledger.resolve(PoolDomain::Local, 1, 0x1000, 8).reasons[0].kind,
        GapKind::EpochMismatch
    );
    assert_eq!(
        ledger.resolve(PoolDomain::Local, 2, 0x1000, 8).reasons[0].kind,
        GapKind::MissingAllocation
    );
}

#[test]
fn locked_local_reset_does_not_invalidate_or_repair_coverage() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 8))
        .unwrap();
    let first = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    ledger.observe_locked_local_reset(0).unwrap();
    let second = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    assert_eq!(ledger.same_span(&first, &second).value, Some(true));
    ledger
        .lose_coverage_from_now(Some(PoolDomain::Local), CoverageLoss::Invalidations)
        .unwrap();
    ledger.observe_locked_local_reset(0).unwrap();
    assert_eq!(ledger.is_live(&first).value, None);
    assert_eq!(ledger.same_allocation(&first, &second).value, Some(true));
    assert!(
        ledger
            .resolve(PoolDomain::Local, 0, 0x1000, 8)
            .value
            .is_none()
    );
}

#[test]
fn incomplete_initial_coverage_and_later_gap_need_an_observed_barrier() {
    for coverage in [
        Coverage::default(),
        Coverage {
            allocations: true,
            invalidations: false,
        },
        Coverage {
            allocations: false,
            invalidations: true,
        },
    ] {
        let mut ledger = Ledger::new(16);
        ledger.begin_pool(PoolDomain::Global, 0, coverage).unwrap();
        assert!(
            ledger
                .observe_allocation(allocation(1, PoolDomain::Global, 0, 0x1000, 8, 8))
                .is_err()
        );
        assert!(
            ledger
                .resolve(PoolDomain::Global, 0, 0x1000, 4)
                .value
                .is_none()
        );
        assert!(
            ledger
                .begin_pool(PoolDomain::Global, 1, Coverage::COMPLETE)
                .is_err()
        );
        ledger
            .observe_invalidation(
                PoolDomain::Global,
                Some(0),
                1,
                InvalidationKind::Release,
                Coverage::COMPLETE,
            )
            .unwrap();
        ledger
            .observe_allocation(allocation(2, PoolDomain::Global, 1, 0x1000, 8, 8))
            .unwrap();
        assert!(
            ledger
                .resolve(PoolDomain::Global, 1, 0x1000, 8)
                .value
                .is_some()
        );
    }
}

#[test]
fn retroactive_loss_invalidates_retired_receipts_and_cannot_disappear_before_begin() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .begin_pool(PoolDomain::Global, 0, Coverage::COMPLETE)
        .unwrap();
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 8))
        .unwrap();
    let old = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(2, PoolDomain::Local, 1, 0x1000, 8, 8))
        .unwrap();
    let new = ledger
        .resolve(PoolDomain::Local, 1, 0x1000, 8)
        .value
        .unwrap();
    ledger
        .observe_allocation(allocation(3, PoolDomain::Global, 0, 0x2000, 8, 8))
        .unwrap();
    let global = ledger
        .resolve(PoolDomain::Global, 0, 0x2000, 8)
        .value
        .unwrap();
    ledger
        .lose_coverage(Some(PoolDomain::Local), CoverageLoss::Both)
        .unwrap();
    assert_eq!(ledger.same_span(&old, &old).value, None);
    assert_eq!(ledger.same_allocation(&old, &new).value, None);
    assert_eq!(ledger.same_span(&global, &global).value, Some(true));
    ledger
        .lose_coverage(Some(PoolDomain::Optimizer), CoverageLoss::Allocations)
        .unwrap();
    assert!(
        ledger
            .begin_pool(PoolDomain::Optimizer, 0, Coverage::COMPLETE)
            .is_err()
    );
    ledger
        .observe_invalidation(
            PoolDomain::Optimizer,
            None,
            0,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(4, PoolDomain::Optimizer, 0, 0x3000, 8, 8))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Optimizer, 0, 0x3000, 8)
            .value
            .is_some()
    );
}

#[test]
fn known_loss_boundary_keeps_only_earlier_historical_receipts() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 8))
        .unwrap();
    let earlier = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    ledger.observe_locked_local_reset(0).unwrap();
    let boundary = ledger.event();
    let later = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 8)
        .value
        .unwrap();
    ledger
        .lose_coverage_since(Some(PoolDomain::Local), CoverageLoss::Allocations, boundary)
        .unwrap();
    assert_eq!(ledger.same_span(&earlier, &earlier).value, Some(true));
    assert_eq!(ledger.same_span(&later, &later).value, None);
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(2, PoolDomain::Local, 1, 0x1000, 8, 8))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Local, 1, 0x1000, 8)
            .value
            .is_some()
    );
}

#[test]
fn reserved_padding_overlap_and_nested_overlap_taint_every_live_candidate() {
    let mut ledger = started(PoolDomain::Local);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 4, 0x100))
        .unwrap();
    let first = ledger
        .resolve(PoolDomain::Local, 0, 0x1000, 4)
        .value
        .unwrap();
    for (id, start, size) in [(2, 0x1040, 8), (3, 0x1060, 8), (4, 0x1044, 0x30)] {
        assert_eq!(
            ledger
                .observe_allocation(allocation(id, PoolDomain::Local, 0, start, size, size))
                .unwrap_err()
                .kind,
            GapKind::Overlap
        );
    }
    for address in [0x1000, 0x1040, 0x1044, 0x1060] {
        assert!(
            ledger
                .resolve(PoolDomain::Local, 0, address, 1)
                .value
                .is_none()
        );
    }
    assert_eq!(ledger.is_live(&first).value, None);
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(5, PoolDomain::Local, 1, 0x1044, 4, 8))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Local, 1, 0x1044, 4)
            .value
            .is_some()
    );
}

#[test]
fn cross_domain_overlap_taint_survives_reset_of_only_one_domain() {
    let mut ledger = started(PoolDomain::Global);
    ledger
        .begin_pool(PoolDomain::Local, 0, Coverage::COMPLETE)
        .unwrap();
    ledger
        .observe_allocation(allocation(1, PoolDomain::Global, 0, 0x1000, 8, 16))
        .unwrap();
    let global = ledger
        .resolve(PoolDomain::Global, 0, 0x1000, 8)
        .value
        .unwrap();
    assert!(
        ledger
            .observe_allocation(allocation(2, PoolDomain::Local, 0, 0x1008, 8, 8))
            .is_err()
    );
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    assert_eq!(ledger.is_live(&global).value, None);
    assert!(
        ledger
            .observe_allocation(allocation(3, PoolDomain::Local, 1, 0x1008, 8, 8))
            .is_err()
    );
    ledger
        .observe_invalidation(
            PoolDomain::Global,
            Some(0),
            1,
            InvalidationKind::Release,
            Coverage::COMPLETE,
        )
        .unwrap();
    // Global retirement does not retroactively make Local's ambiguous birth known.
    assert!(
        ledger
            .resolve(PoolDomain::Local, 1, 0x1008, 8)
            .value
            .is_none()
    );
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(1),
            2,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    ledger
        .observe_allocation(allocation(4, PoolDomain::Local, 2, 0x1008, 8, 8))
        .unwrap();
}

#[test]
fn invalid_spans_and_epoch_mismatch_do_not_leave_current_identity_proven() {
    for (address, requested, reserved) in [
        (0, 8, 8),
        (0x1000, 8, 0),
        (0x1000, 9, 8),
        (u32::MAX - 3, 8, 8),
    ] {
        let mut ledger = started(PoolDomain::Heap);
        ledger
            .observe_allocation(allocation(1, PoolDomain::Heap, 0, 0x2000, 8, 8))
            .unwrap();
        let first = ledger
            .resolve(PoolDomain::Heap, 0, 0x2000, 8)
            .value
            .unwrap();
        assert_eq!(
            ledger
                .observe_allocation(allocation(
                    2,
                    PoolDomain::Heap,
                    0,
                    address,
                    requested,
                    reserved
                ))
                .unwrap_err()
                .kind,
            GapKind::InvalidSpan
        );
        assert_eq!(ledger.is_live(&first).value, None);
    }
    let mut ledger = started(PoolDomain::Heap);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Heap, 0, 0x2000, 8, 8))
        .unwrap();
    assert!(
        ledger
            .observe_allocation(allocation(2, PoolDomain::Heap, 1, 0x3000, 8, 8))
            .is_err()
    );
    assert!(
        ledger
            .resolve(PoolDomain::Heap, 0, 0x2000, 8)
            .value
            .is_none()
    );
}

#[test]
fn global_id_order_and_history_capacity_are_fail_closed() {
    for next in [0, 1] {
        let mut ledger = started(PoolDomain::Local);
        ledger
            .begin_pool(PoolDomain::Global, 0, Coverage::COMPLETE)
            .unwrap();
        ledger
            .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 8))
            .unwrap();
        assert_eq!(
            ledger
                .observe_allocation(allocation(next, PoolDomain::Global, 0, 0x2000, 8, 8))
                .unwrap_err()
                .kind,
            GapKind::AllocationIdOrder
        );
        assert_eq!(ledger.allocation_count(), 1);
        assert!(
            ledger
                .resolve(PoolDomain::Local, 0, 0x1000, 8)
                .value
                .is_none()
        );
    }
    let mut ledger = Ledger::new(1);
    ledger
        .begin_pool(PoolDomain::Local, 0, Coverage::COMPLETE)
        .unwrap();
    ledger
        .observe_allocation(allocation(1, PoolDomain::Local, 0, 0x1000, 8, 8))
        .unwrap();
    ledger
        .observe_invalidation(
            PoolDomain::Local,
            Some(0),
            1,
            InvalidationKind::Reset,
            Coverage::COMPLETE,
        )
        .unwrap();
    assert_eq!(
        ledger
            .observe_allocation(allocation(2, PoolDomain::Local, 1, 0x2000, 8, 8))
            .unwrap_err()
            .kind,
        GapKind::Capacity
    );
    assert_eq!(ledger.allocation_count(), 1);
}

#[test]
fn identical_ids_addresses_and_serialized_receipts_from_other_ledgers_cannot_join() {
    let mut first = started(PoolDomain::Global);
    let mut second = started(PoolDomain::Global);
    for ledger in [&mut first, &mut second] {
        ledger
            .observe_allocation(allocation(1, PoolDomain::Global, 0, 0x1000, 8, 8))
            .unwrap();
    }
    let a = first
        .resolve(PoolDomain::Global, 0, 0x1000, 8)
        .value
        .unwrap();
    let b = second
        .resolve(PoolDomain::Global, 0, 0x1000, 8)
        .value
        .unwrap();
    assert_eq!(
        serde_json::to_value(&a).unwrap(),
        serde_json::to_value(&b).unwrap()
    );
    assert_eq!(
        first.same_span(&a, &b).reasons[0].kind,
        GapKind::ForeignReceipt
    );
    assert_eq!(second.is_live(&a).reasons[0].kind, GapKind::ForeignReceipt);
    assert_eq!(first.same_span(&a, &a.clone()).value, Some(true));
}

#[test]
fn zero_requested_extent_and_last_address_do_not_grant_padding_access() {
    let mut ledger = started(PoolDomain::Block);
    ledger
        .observe_allocation(allocation(1, PoolDomain::Block, 0, 0x1000, 0, 8))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Block, 0, 0x1000, 0)
            .value
            .is_some()
    );
    assert!(
        ledger
            .resolve(PoolDomain::Block, 0, 0x1000, 1)
            .value
            .is_none()
    );
    assert!(
        ledger
            .resolve(PoolDomain::Block, 0, 0x1001, 0)
            .value
            .is_none()
    );
    ledger
        .observe_allocation(allocation(2, PoolDomain::Block, 0, u32::MAX, 1, 1))
        .unwrap();
    assert!(
        ledger
            .resolve(PoolDomain::Block, 0, u32::MAX, 1)
            .value
            .is_some()
    );
    assert!(
        ledger
            .resolve(PoolDomain::Block, 0, u32::MAX, 2)
            .value
            .is_none()
    );
}

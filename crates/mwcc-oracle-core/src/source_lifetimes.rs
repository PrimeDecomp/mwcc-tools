//! Incremental host ledger for guarded compiler allocation observations.
//!
//! This module does not observe the compiler, parse runtime records, or establish
//! producer coverage. Callers must supply audited allocation and invalidation
//! observations and report lost coverage explicitly. Requested bytes are an
//! accessible payload extent, not initialized bytes or evidence of an object type.
//! An allocation identity and a node's immutable observation/revision are separate:
//! this ledger deliberately provides no node/content equivalence operation.
use serde::Serialize;
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::Arc,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PoolDomain {
    Heap,
    Optimizer,
    Block,
    Local,
    Global,
}
const POOLS: [PoolDomain; 5] = [
    PoolDomain::Heap,
    PoolDomain::Optimizer,
    PoolDomain::Block,
    PoolDomain::Local,
    PoolDomain::Global,
];

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize)]
pub struct AllocationId(pub u64);

/// An assertion supplied by the guarded observation producer/parser, not a
/// conclusion obtained by this ledger from seeing some successful allocations.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
pub struct Coverage {
    pub allocations: bool,
    pub invalidations: bool,
}
impl Coverage {
    pub const COMPLETE: Self = Self {
        allocations: true,
        invalidations: true,
    };
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub struct Allocation {
    pub id: AllocationId,
    pub pool: PoolDomain,
    pub epoch: u64,
    pub address: u32,
    pub requested: u32,
    pub reserved: u32,
}
impl Allocation {
    fn end(self) -> Option<u64> {
        let end = u64::from(self.address) + u64::from(self.reserved);
        (self.address != 0
            && self.reserved != 0
            && self.requested <= self.reserved
            && end <= 1u64 << 32)
            .then_some(end)
    }
    fn contains(self, address: u32, extent: u32) -> bool {
        let start = u64::from(address);
        let base = u64::from(self.address);
        // Zero-byte receipts identify an address inside the payload, or the base
        // of a zero-request allocation. They never identify a one-past pointer.
        start >= base
            && (start < base + u64::from(self.requested)
                || (self.requested == 0 && extent == 0 && start == base))
            && start + u64::from(extent) <= base + u64::from(self.requested)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum GapKind {
    MissingPool,
    EpochMismatch,
    AllocationCoverage,
    InvalidationCoverage,
    AllocationIdOrder,
    InvalidSpan,
    Overlap,
    MissingAllocation,
    OutsideRequestedExtent,
    ForeignReceipt,
    InvalidReceipt,
    Capacity,
    EventOrder,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Reason {
    pub kind: GapKind,
    pub pool: Option<PoolDomain>,
    pub allocation: Option<AllocationId>,
    pub detail: String,
}
fn reason(
    kind: GapKind,
    pool: Option<PoolDomain>,
    allocation: Option<AllocationId>,
    detail: &str,
) -> Reason {
    Reason {
        kind,
        pool,
        allocation,
        detail: detail.into(),
    }
}

/// `None` is indeterminate, not a negative conclusion.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Evidence<T> {
    pub value: Option<T>,
    pub reasons: Vec<Reason>,
}
impl<T> Evidence<T> {
    fn known(value: T) -> Self {
        Self {
            value: Some(value),
            reasons: vec![],
        }
    }
    fn unknown(reason: Reason) -> Self {
        Self {
            value: None,
            reasons: vec![reason],
        }
    }
}

/// A receipt issued by one ledger at an observed point in its event sequence.
/// Private fields and the non-serializable identity token prevent receipts from
/// being fabricated or imported into another ledger/capture through safe APIs.
/// Serialize is for display only; there is deliberately no Deserialize impl.
#[derive(Clone, Debug, Serialize)]
pub struct AllocationReference {
    #[serde(skip)]
    ledger: Arc<()>,
    allocation: AllocationId,
    pool: PoolDomain,
    epoch: u64,
    offset: u32,
    extent: u32,
    observed_at: u64,
}
impl AllocationReference {
    pub fn allocation(&self) -> AllocationId {
        self.allocation
    }
    pub fn pool(&self) -> PoolDomain {
        self.pool
    }
    pub fn epoch(&self) -> u64 {
        self.epoch
    }
    pub fn offset(&self) -> u32 {
        self.offset
    }
    pub fn extent(&self) -> u32 {
        self.extent
    }
    pub fn observed_at(&self) -> u64 {
        self.observed_at
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CoverageLoss {
    Allocations,
    Invalidations,
    Both,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum InvalidationKind {
    Reset,
    Release,
}

struct Pool {
    epoch: u64,
    coverage: Coverage,
    gap: Option<Reason>,
}
struct Record {
    allocation: Allocation,
    born: u64,
    retired: Option<(u64, InvalidationKind)>,
    taint: Option<(u64, Reason)>,
}

// Active spans only. An augmented AVL avoids both a historical scan and the
// unsound predecessor-only lookup when one span contains several other spans.
type Link = Option<Box<Interval>>;
struct Interval {
    key: (u32, AllocationId),
    end: u64,
    max_end: u64,
    height: i32,
    left: Link,
    right: Link,
}
fn height(link: &Link) -> i32 {
    link.as_ref().map_or(0, |n| n.height)
}
fn max_end(link: &Link) -> u64 {
    link.as_ref().map_or(0, |n| n.max_end)
}
impl Interval {
    fn update(&mut self) {
        self.height = 1 + height(&self.left).max(height(&self.right));
        self.max_end = self.end.max(max_end(&self.left)).max(max_end(&self.right));
    }
}
fn rotate_left(mut node: Box<Interval>) -> Box<Interval> {
    let mut right = node.right.take().unwrap();
    node.right = right.left.take();
    node.update();
    right.left = Some(node);
    right.update();
    right
}
fn rotate_right(mut node: Box<Interval>) -> Box<Interval> {
    let mut left = node.left.take().unwrap();
    node.left = left.right.take();
    node.update();
    left.right = Some(node);
    left.update();
    left
}
fn balance(mut node: Box<Interval>) -> Box<Interval> {
    node.update();
    if height(&node.left) - height(&node.right) > 1 {
        let left = node.left.as_ref().unwrap();
        if height(&left.left) < height(&left.right) {
            node.left = node.left.take().map(rotate_left)
        }
        return rotate_right(node);
    }
    if height(&node.right) - height(&node.left) > 1 {
        let right = node.right.as_ref().unwrap();
        if height(&right.right) < height(&right.left) {
            node.right = node.right.take().map(rotate_right)
        }
        return rotate_left(node);
    }
    node
}
fn insert_interval(root: Link, key: (u32, AllocationId), end: u64) -> Box<Interval> {
    let Some(mut node) = root else {
        return Box::new(Interval {
            key,
            end,
            max_end: end,
            height: 1,
            left: None,
            right: None,
        });
    };
    if key < node.key {
        node.left = Some(insert_interval(node.left.take(), key, end))
    } else {
        node.right = Some(insert_interval(node.right.take(), key, end))
    }
    balance(node)
}
fn take_first(mut node: Box<Interval>) -> ((u32, AllocationId), u64, Link) {
    let Some(left) = node.left.take() else {
        return (node.key, node.end, node.right.take());
    };
    let (key, end, left) = take_first(left);
    node.left = left;
    (key, end, Some(balance(node)))
}
fn remove_interval(root: Link, key: (u32, AllocationId)) -> Link {
    let mut node = root?;
    match key.cmp(&node.key) {
        std::cmp::Ordering::Less => node.left = remove_interval(node.left.take(), key),
        std::cmp::Ordering::Greater => node.right = remove_interval(node.right.take(), key),
        std::cmp::Ordering::Equal => {
            if node.left.is_none() {
                return node.right.take();
            }
            let Some(right) = node.right.take() else {
                return node.left.take();
            };
            let (next, end, right) = take_first(right);
            node.key = next;
            node.end = end;
            node.right = right;
        }
    }
    Some(balance(node))
}
fn overlapping(root: &Link, start: u64, end: u64, result: &mut Vec<AllocationId>) {
    let Some(node) = root else { return };
    if node.max_end <= start {
        return;
    }
    overlapping(&node.left, start, end, result);
    if u64::from(node.key.0) >= end {
        return;
    }
    if node.end > start {
        result.push(node.key.1)
    }
    overlapping(&node.right, start, end, result);
}
#[derive(Default)]
struct IntervalIndex {
    root: Link,
}
impl IntervalIndex {
    fn insert(&mut self, start: u32, id: AllocationId, end: u64) {
        self.root = Some(insert_interval(self.root.take(), (start, id), end))
    }
    fn remove(&mut self, start: u32, id: AllocationId) {
        self.root = remove_interval(self.root.take(), (start, id))
    }
    fn overlaps(&self, start: u64, end: u64) -> Vec<AllocationId> {
        let mut result = vec![];
        overlapping(&self.root, start, end, &mut result);
        result
    }
}

/// The allocation bound applies to the entire history, including expired records.
/// Records are not evicted or reused: doing so could make old receipts ambiguous.
/// Pool state is bounded to the five audited domains. Receipt storage is caller-owned.
/// Normal allocation/lookup uses an interval index of active spans and a
/// logarithmic retained-history lookup, without scanning historical records.
/// Overlap reporting visits the matching interval-tree paths. Coverage failures
/// may scan historical records to invalidate earlier receipts retrospectively.
pub struct Ledger {
    identity: Arc<()>,
    limit: usize,
    records: BTreeMap<AllocationId, Record>,
    pools: BTreeMap<PoolDomain, Pool>,
    event: u64,
    last_id: u64,
    fatal: Option<Reason>,
    pending_gaps: BTreeMap<PoolDomain, Reason>,
    active: IntervalIndex,
    active_by_pool: BTreeMap<PoolDomain, BTreeSet<AllocationId>>,
}
impl Ledger {
    pub fn new(max_allocations: usize) -> Self {
        Self {
            identity: Arc::new(()),
            limit: max_allocations,
            records: BTreeMap::new(),
            pools: BTreeMap::new(),
            event: 0,
            last_id: 0,
            fatal: None,
            pending_gaps: BTreeMap::new(),
            active: IntervalIndex::default(),
            active_by_pool: POOLS.into_iter().map(|p| (p, BTreeSet::new())).collect(),
        }
    }
    pub fn allocation_count(&self) -> usize {
        self.records.len()
    }
    pub fn event(&self) -> u64 {
        self.event
    }

    fn tick(&mut self) -> Result<u64, Reason> {
        if let Some(gap) = &self.fatal {
            return Err(gap.clone());
        }
        let Some(next) = self.event.checked_add(1) else {
            let gap = reason(GapKind::EventOrder, None, None, "event sequence exhausted");
            self.fatal = Some(gap.clone());
            return Err(gap);
        };
        self.event = next;
        Ok(next)
    }
    fn taint_pool(&mut self, pool: PoolDomain, gap: Reason) {
        // Existing active records were already tainted, and every subsequent
        // allocation inherits this gap. Avoid a new active scan per bad event.
        if self
            .pools
            .get(&pool)
            .is_some_and(|state| state.gap.is_some())
            || self.pending_gaps.contains_key(&pool)
        {
            return;
        }
        for id in &self.active_by_pool[&pool] {
            let record = self.records.get_mut(id).unwrap();
            record.taint.get_or_insert((self.event, gap.clone()));
        }
        if let Some(state) = self.pools.get_mut(&pool) {
            state.gap.get_or_insert(gap);
        } else {
            self.pending_gaps.entry(pool).or_insert(gap);
        }
    }
    fn pool_gap(&self, pool: PoolDomain, epoch: u64) -> Option<Reason> {
        if let Some(gap) = &self.fatal {
            return Some(gap.clone());
        }
        let Some(state) = self.pools.get(&pool) else {
            return Some(reason(
                GapKind::MissingPool,
                Some(pool),
                None,
                "pool observation scope has not begun",
            ));
        };
        if state.epoch != epoch {
            return Some(reason(
                GapKind::EpochMismatch,
                Some(pool),
                None,
                "observation does not belong to the current pool epoch",
            ));
        }
        if let Some(gap) = &state.gap {
            return Some(gap.clone());
        }
        if !state.coverage.allocations {
            return Some(reason(
                GapKind::AllocationCoverage,
                Some(pool),
                None,
                "allocation observation coverage is unknown",
            ));
        }
        if !state.coverage.invalidations {
            return Some(reason(
                GapKind::InvalidationCoverage,
                Some(pool),
                None,
                "invalidation observation coverage is unknown",
            ));
        }
        None
    }

    /// Declare the first guarded observation scope for a pool. This does not
    /// assign identities to pre-existing allocations or erase earlier gaps. It
    /// cannot restart a pool; use an actually observed invalidation (including
    /// `epoch=None` for a first guarded initialization) to establish a barrier.
    pub fn begin_pool(
        &mut self,
        pool: PoolDomain,
        epoch: u64,
        coverage: Coverage,
    ) -> Result<(), Reason> {
        self.tick()?;
        if self.pools.contains_key(&pool) {
            let gap = reason(
                GapKind::EpochMismatch,
                Some(pool),
                None,
                "pool scope restarted without an invalidating barrier",
            );
            self.taint_pool(pool, gap.clone());
            return Err(gap);
        }
        if let Some(gap) = self.pending_gaps.get(&pool) {
            return Err(gap.clone());
        }
        self.pools.insert(
            pool,
            Pool {
                epoch,
                coverage,
                gap: None,
            },
        );
        Ok(())
    }

    /// IDs must be nonzero and globally increasing across every pool. Gaps in
    /// numbering alone are not treated as proof of lost observations; the future
    /// parser must reconcile actual stream totals and report coverage loss.
    pub fn observe_allocation(&mut self, allocation: Allocation) -> Result<AllocationId, Reason> {
        let now = self.tick()?;
        if allocation.id.0 == 0 || allocation.id.0 <= self.last_id {
            let gap = reason(
                GapKind::AllocationIdOrder,
                Some(allocation.pool),
                Some(allocation.id),
                "allocation IDs are not globally increasing",
            );
            self.fatal = Some(gap.clone());
            return Err(gap);
        }
        self.last_id = allocation.id.0;
        let Some(end) = allocation.end() else {
            let gap = reason(
                GapKind::InvalidSpan,
                Some(allocation.pool),
                Some(allocation.id),
                "invalid requested/reserved 32-bit allocation span",
            );
            // Inconsistent size/address observations do not establish a safe
            // footprint. It may collide with a different pool's live spans;
            // neither ignoring it nor trusting the smaller size is sound.
            // Each affected pool needs a fresh observed barrier before joining.
            for pool in POOLS {
                self.taint_pool(pool, gap.clone());
            }
            return Err(gap);
        };
        if self.records.len() == self.limit {
            let gap = reason(
                GapKind::Capacity,
                Some(allocation.pool),
                Some(allocation.id),
                "allocation history capacity exceeded",
            );
            self.fatal = Some(gap.clone());
            return Err(gap);
        }
        let mut gap = self.pool_gap(allocation.pool, allocation.epoch);
        // A late allocation in the wrong epoch may overwrite any live identity
        // in that domain. Do not let a failed input leave those receipts usable.
        if let Some(g) = &gap {
            self.taint_pool(allocation.pool, g.clone());
        }
        for id in self.active.overlaps(u64::from(allocation.address), end) {
            let previous = self.records.get_mut(&id).unwrap();
            let old = previous.allocation;
            let overlap = reason(
                GapKind::Overlap,
                Some(old.pool),
                Some(old.id),
                "live reserved allocation spans overlap without an invalidating barrier",
            );
            previous.taint.get_or_insert((now, overlap));
            gap = Some(reason(
                GapKind::Overlap,
                Some(allocation.pool),
                Some(allocation.id),
                "live reserved allocation spans overlap without an invalidating barrier",
            ));
        }
        self.active.insert(allocation.address, allocation.id, end);
        self.active_by_pool
            .get_mut(&allocation.pool)
            .unwrap()
            .insert(allocation.id);
        self.records.insert(
            allocation.id,
            Record {
                allocation,
                born: now,
                retired: None,
                taint: gap.clone().map(|g| (now, g)),
            },
        );
        match gap {
            Some(gap) => Err(gap),
            None => Ok(allocation.id),
        }
    }

    /// Retrospective coverage loss: invalidate affected receipts throughout the
    /// capture, including already retired epochs. Use this when missing totals
    /// are discovered at end and the missing events cannot be located precisely.
    /// A future covered barrier repairs only its own future epoch.
    pub fn lose_coverage(
        &mut self,
        pool: Option<PoolDomain>,
        lost: CoverageLoss,
    ) -> Result<(), Reason> {
        self.coverage_loss(pool, lost, Some(0))
    }

    /// Invalidate affected observations at/after a known ledger event. Earlier
    /// historical receipts remain valid. The caller must justify this boundary.
    pub fn lose_coverage_since(
        &mut self,
        pool: Option<PoolDomain>,
        lost: CoverageLoss,
        since_event: u64,
    ) -> Result<(), Reason> {
        self.coverage_loss(pool, lost, Some(since_event))
    }

    /// Precisely located loss beginning with this new event; unlike a missing
    /// end total, this does not retrospectively invalidate earlier receipts.
    pub fn lose_coverage_from_now(
        &mut self,
        pool: Option<PoolDomain>,
        lost: CoverageLoss,
    ) -> Result<(), Reason> {
        self.coverage_loss(pool, lost, None)
    }

    fn coverage_loss(
        &mut self,
        pool: Option<PoolDomain>,
        lost: CoverageLoss,
        since: Option<u64>,
    ) -> Result<(), Reason> {
        let now = self.tick()?;
        let since = since.unwrap_or(now);
        if since > now {
            let gap = reason(
                GapKind::EventOrder,
                pool,
                None,
                "coverage loss boundary lies in the future",
            );
            self.fatal = Some(gap.clone());
            return Err(gap);
        }
        for domain in POOLS
            .into_iter()
            .filter(|p| pool.is_none_or(|selected| selected == *p))
        {
            let gap = reason(
                if lost == CoverageLoss::Allocations {
                    GapKind::AllocationCoverage
                } else {
                    GapKind::InvalidationCoverage
                },
                Some(domain),
                None,
                "guarded allocation or invalidation observations were lost",
            );
            if let Some(state) = self.pools.get_mut(&domain) {
                if matches!(lost, CoverageLoss::Allocations | CoverageLoss::Both) {
                    state.coverage.allocations = false;
                }
                if matches!(lost, CoverageLoss::Invalidations | CoverageLoss::Both) {
                    state.coverage.invalidations = false;
                }
                state.gap.get_or_insert(gap.clone());
            } else {
                self.pending_gaps.entry(domain).or_insert(gap.clone());
            }
            // Failure-only historical scan: unlike normal lookups this must also
            // invalidate retired epochs if the loss was discovered retrospectively.
            for record in self.records.values_mut().filter(|r| {
                r.allocation.pool == domain && r.retired.is_none_or(|(end, _)| end > since)
            }) {
                let at = since.max(record.born);
                if record.taint.as_ref().is_none_or(|(old, _)| at < *old) {
                    record.taint = Some((at, gap.clone()));
                }
            }
        }
        Ok(())
    }

    /// Apply an actually observed invalidating reset/release, not a pool-selection
    /// call or an assumed consequence of some other arena's reset. `epoch=None`
    /// requires no prior pool scope and means an observed initial invalidation;
    /// it can repair coverage loss reported before begin_pool.
    pub fn observe_invalidation(
        &mut self,
        pool: PoolDomain,
        epoch: Option<u64>,
        next_epoch: u64,
        kind: InvalidationKind,
        coverage_after: Coverage,
    ) -> Result<(), Reason> {
        let now = self.tick()?;
        if self.pools.get(&pool).map(|p| p.epoch) != epoch
            || epoch.is_some_and(|old| next_epoch <= old)
        {
            let gap = reason(
                GapKind::EpochMismatch,
                Some(pool),
                None,
                "invalidating barrier has missing/mismatched or non-increasing epoch",
            );
            self.taint_pool(pool, gap.clone());
            return Err(gap);
        }
        let retired = std::mem::take(self.active_by_pool.get_mut(&pool).unwrap());
        for id in retired {
            let record = self.records.get_mut(&id).unwrap();
            self.active.remove(record.allocation.address, id);
            record.retired = Some((now, kind));
        }
        self.pending_gaps.remove(&pool);
        self.pools.insert(
            pool,
            Pool {
                epoch: next_epoch,
                coverage: coverage_after,
                gap: None,
            },
        );
        Ok(())
    }

    /// Retail freelheap with a verified nonzero lock does not invalidate Local.
    /// This observation advances ordering only; it neither changes the epoch nor
    /// repairs missing coverage. No lock state is inferred by the ledger.
    pub fn observe_locked_local_reset(&mut self, epoch: u64) -> Result<(), Reason> {
        self.tick()?;
        if self
            .pools
            .get(&PoolDomain::Local)
            .is_none_or(|p| p.epoch != epoch)
        {
            let gap = reason(
                GapKind::EpochMismatch,
                Some(PoolDomain::Local),
                None,
                "locked local reset has no matching observed epoch",
            );
            self.taint_pool(PoolDomain::Local, gap.clone());
            return Err(gap);
        }
        Ok(())
    }

    /// Resolve only a currently covered live requested payload. `extent` is the
    /// required accessible byte count; a known receipt does not authorize reading
    /// uninitialized bytes or treating the allocation as any particular C type.
    pub fn resolve(
        &self,
        pool: PoolDomain,
        epoch: u64,
        address: u32,
        extent: u32,
    ) -> Evidence<AllocationReference> {
        if let Some(gap) = self.pool_gap(pool, epoch) {
            return Evidence::unknown(gap);
        }
        let candidates = self
            .active
            .overlaps(u64::from(address), u64::from(address) + 1)
            .into_iter()
            .map(|id| &self.records[&id])
            .filter(|r| r.allocation.pool == pool && r.allocation.epoch == epoch)
            .collect::<Vec<_>>();
        if candidates.len() != 1 {
            return Evidence::unknown(reason(
                if candidates.is_empty() {
                    GapKind::MissingAllocation
                } else {
                    GapKind::Overlap
                },
                Some(pool),
                None,
                "address does not have one observed live allocation",
            ));
        }
        let record = candidates[0];
        if let Some((_, gap)) = &record.taint {
            return Evidence::unknown(gap.clone());
        }
        if !record.allocation.contains(address, extent) {
            return Evidence::unknown(reason(
                GapKind::OutsideRequestedExtent,
                Some(pool),
                Some(record.allocation.id),
                "requested span reaches allocator padding or exceeds the payload",
            ));
        }
        Evidence::known(AllocationReference {
            ledger: self.identity.clone(),
            allocation: record.allocation.id,
            pool,
            epoch,
            offset: address - record.allocation.address,
            extent,
            observed_at: self.event,
        })
    }

    fn validate(&self, reference: &AllocationReference) -> Result<&Record, Reason> {
        if let Some(gap) = &self.fatal {
            return Err(gap.clone());
        }
        if !Arc::ptr_eq(&self.identity, &reference.ledger) {
            return Err(reason(
                GapKind::ForeignReceipt,
                Some(reference.pool),
                Some(reference.allocation),
                "receipt belongs to another ledger/capture",
            ));
        }
        let Some(record) = self.records.get(&reference.allocation) else {
            return Err(reason(
                GapKind::InvalidReceipt,
                Some(reference.pool),
                Some(reference.allocation),
                "receipt allocation is not retained",
            ));
        };
        let allocation = record.allocation;
        if reference.pool != allocation.pool
            || reference.epoch != allocation.epoch
            || reference.observed_at < record.born
            || reference.observed_at > self.event
            || record
                .retired
                .is_some_and(|(end, _)| reference.observed_at >= end)
            || allocation
                .address
                .checked_add(reference.offset)
                .is_none_or(|address| !allocation.contains(address, reference.extent))
        {
            return Err(reason(
                GapKind::InvalidReceipt,
                Some(reference.pool),
                Some(reference.allocation),
                "receipt is outside its observed allocation lifetime/payload",
            ));
        }
        if let Some((at, gap)) = &record.taint
            && reference.observed_at >= *at
        {
            return Err(gap.clone());
        }
        Ok(record)
    }

    /// Compare historical receipts, including receipts whose allocations have
    /// since expired. This establishes only observed allocation identity, never
    /// current liveness, equal contents, source roles or transformation equivalence.
    pub fn same_allocation(
        &self,
        left: &AllocationReference,
        right: &AllocationReference,
    ) -> Evidence<bool> {
        for reference in [left, right] {
            if let Err(gap) = self.validate(reference) {
                return Evidence::unknown(gap);
            }
        }
        Evidence::known(left.allocation == right.allocation)
    }
    pub fn same_span(
        &self,
        left: &AllocationReference,
        right: &AllocationReference,
    ) -> Evidence<bool> {
        let identity = self.same_allocation(left, right);
        match identity.value {
            Some(same) => {
                Evidence::known(same && left.offset == right.offset && left.extent == right.extent)
            }
            None => identity,
        }
    }

    /// Current liveness is deliberately separate from historical identity.
    pub fn is_live(&self, reference: &AllocationReference) -> Evidence<bool> {
        let record = match self.validate(reference) {
            Ok(record) => record,
            Err(gap) => return Evidence::unknown(gap),
        };
        if record.retired.is_some() {
            return Evidence::known(false);
        }
        if let Some(gap) = self.pool_gap(reference.pool, reference.epoch) {
            return Evidence::unknown(gap);
        }
        if let Some((_, gap)) = &record.taint {
            return Evidence::unknown(gap.clone());
        }
        Evidence::known(true)
    }
}

#[cfg(test)]
mod interval_tests {
    use super::*;

    fn next(seed: &mut u64) -> u64 {
        *seed = seed
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        *seed
    }
    fn check_tree(
        node: &Link,
        low: Option<(u32, AllocationId)>,
        high: Option<(u32, AllocationId)>,
    ) -> (i32, u64, usize) {
        let Some(node) = node else { return (0, 0, 0) };
        assert!(low.is_none_or(|key| key < node.key));
        assert!(high.is_none_or(|key| node.key < key));
        let (lh, lm, ln) = check_tree(&node.left, low, Some(node.key));
        let (rh, rm, rn) = check_tree(&node.right, Some(node.key), high);
        assert!((lh - rh).abs() <= 1);
        assert_eq!(node.height, 1 + lh.max(rh));
        assert_eq!(node.max_end, node.end.max(lm).max(rm));
        (node.height, node.max_end, 1 + ln + rn)
    }

    #[test]
    fn randomized_active_intervals_match_independent_brute_force_with_pool_retirements() {
        let mut index = IntervalIndex::default();
        let mut oracle: BTreeMap<AllocationId, (usize, u32, u64)> = BTreeMap::new();
        let mut seed = 0x736f757263655f69u64;
        let mut id = 0u64;
        for step in 0..20_000 {
            let choice = next(&mut seed) % 16;
            if choice < 10 && oracle.len() < 1024 {
                id += 1;
                let pool = (next(&mut seed) % 5) as usize;
                let (start, end) = if id.is_multiple_of(17) {
                    (0x1000, 0x40000)
                } else {
                    let start = 0x1000 + (next(&mut seed) % 0x20000) as u32;
                    (start, u64::from(start) + 1 + next(&mut seed) % 0x1000)
                };
                index.insert(start, AllocationId(id), end);
                oracle.insert(AllocationId(id), (pool, start, end));
            } else if choice == 15 {
                // An arena reset retires only its domain, including intervals
                // that contain/overlap still-live allocations of other domains.
                let pool = (next(&mut seed) % 5) as usize;
                oracle.retain(|id, (domain, start, _)| {
                    if *domain == pool {
                        index.remove(*start, *id);
                        false
                    } else {
                        true
                    }
                });
            } else if !oracle.is_empty() {
                let at = (next(&mut seed) % oracle.len() as u64) as usize;
                let (&id, &(_, start, _)) = oracle.iter().nth(at).unwrap();
                index.remove(start, id);
                oracle.remove(&id);
            }
            for _ in 0..3 {
                let start = next(&mut seed) % 0x42000;
                let end = start + 1 + next(&mut seed) % 0x3000;
                let mut observed = index.overlaps(start, end);
                observed.sort();
                let expected = oracle
                    .iter()
                    .filter_map(|(&id, &(_, a, b))| (u64::from(a) < end && start < b).then_some(id))
                    .collect::<Vec<_>>();
                assert_eq!(observed, expected, "step {step} query {start:x}..{end:x}");
            }
            if step % 97 == 0 {
                let (h, _, count) = check_tree(&index.root, None, None);
                assert_eq!(count, oracle.len());
                assert!(h <= 2 * ((count + 1).ilog2() as i32 + 1));
            }
        }
        for (id, (_, start, _)) in oracle {
            index.remove(start, id)
        }
        assert!(index.root.is_none());
    }
}

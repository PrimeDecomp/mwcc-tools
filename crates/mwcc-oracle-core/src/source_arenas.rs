//! Compile-local guarded arena observations, separate from backend function scopes.
//!
//! Checkpoints reconcile independently sampled cumulative counters. A balanced
//! textual prefix alone never establishes coverage. Queries replay the retained
//! raw records into the allocation ledger; deserialized display fields are not
//! trusted as allocation receipts. Allocation identity proves neither initialized
//! bytes, original source ancestry nor equivalence of mutable nodes.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    source_lifetimes::{Allocation, AllocationId, Coverage, InvalidationKind, Ledger, PoolDomain},
    trace::{self, Record},
};
use anyhow::{Result, anyhow, ensure};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

pub const SCOPE: &str = "guarded_retail_arenas_v1";
const MAX_EVENTS: usize = 2_000_000;
const MAX_DEPTH: usize = 64;

fn gap(detail: impl Into<String>) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::SourceArenaCoverage, detail)
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
pub fn is_record(kind: &str) -> bool {
    kind.starts_with("pv.arena.")
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Request {
    pub request: u32,
    pub parent_request: u32,
    pub pair: u8,
    pub pool: u8,
    pub requested: u32,
    pub retail_esp: u32,
    pub site: u32,
    pub epoch: u32,
    pub depth: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Commit {
    pub allocation: u32,
    pub request: u32,
    pub pair: u8,
    pub pool: u8,
    pub requested: u32,
    pub requested_known: bool,
    pub reserved: u32,
    pub address: u32,
    pub block: u32,
    pub freep: u32,
    pub free: i32,
    pub retail_esp: u32,
    pub site: u32,
    pub epoch: u32,
    pub complete: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Reset {
    pub reset: u32,
    pub parent_reset: u32,
    pub op: u8,
    pub mask: u8,
    pub skip: bool,
    pub request_depth: u32,
    pub depth: u32,
    pub epochs: [u32; 5],
    pub lock_value: Option<u16>,
    pub status_known: Option<bool>,
    pub status: Option<u32>,
}
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Totals {
    pub checkpoints: u32,
    pub requests: u32,
    pub allocations: u32,
    pub resets: u32,
    pub reset_ends: u32,
    pub gaps: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Checkpoint {
    pub trace_position: usize,
    pub event: u32,
    pub totals: Totals,
    pub request_depth: u32,
    pub reset_depth: u32,
    pub unsupported: bool,
    pub function: u32,
    pub epochs: [u32; 5],
    pub observations: Evaluation,
}
/// Reconciled event coverage only. A pool still needs an observed invalidation
/// boundary, and points inside a reset are never queryable.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CoverageInterval {
    pub begin_position: usize,
    pub end_position: usize,
    pub checkpoint_position: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum EventKind {
    Begin { version: u8, pools: u32, lock: u32 },
    Request(Request),
    Allocation(Commit),
    ResetBegin(Reset),
    ResetEnd(Reset),
    Gap { reason: String, mask: u8, site: u32 },
    Checkpoint(Checkpoint),
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Event {
    pub trace_position: usize,
    pub event: u32,
    pub observation: EventKind,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct History {
    pub format_version: u32,
    pub scope: Option<String>,
    pub events: Vec<Event>,
    pub checkpoints: Vec<Checkpoint>,
    pub coverage_intervals: Vec<CoverageInterval>,
    pub observations: Evaluation,
    /// Authoritative retained input. Preparation revalidates this sequence.
    pub records: Vec<Record>,
}
impl Default for History {
    fn default() -> Self {
        Self {
            format_version: 1,
            scope: None,
            events: vec![],
            checkpoints: vec![],
            coverage_intervals: vec![],
            observations: gap("no guarded arena stream"),
            records: vec![],
        }
    }
}
/// Display of a ledger-issued receipt at the requested historical position.
/// This is not a receipt that can be fed into another ledger or capture.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ResolvedAllocation {
    pub allocation: u64,
    pub pool: PoolDomain,
    pub epoch: u64,
    pub offset: u32,
    pub extent: u32,
    pub trace_position: usize,
    pub checkpoint_position: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Resolution {
    pub allocation: Option<ResolvedAllocation>,
    pub observations: Evaluation,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Query {
    pub trace_position: usize,
    pub pool: PoolDomain,
    pub address: u32,
    pub extent: u32,
}

/// A receipt issued for one immutable prepared capture at a historical point.
/// It establishes allocation identity only, never current liveness, initialized
/// bytes, unchanged contents or node/transformation equivalence. Private fields
/// and the absence of Deserialize prevent importing display data as a receipt.
#[derive(Clone, Debug, Serialize)]
pub struct HistoricalReference {
    #[serde(skip)]
    capture: Arc<()>,
    query: Query,
    resolved: ResolvedAllocation,
}
impl HistoricalReference {
    pub fn query(&self) -> Query {
        self.query
    }
    pub fn trace_position(&self) -> usize {
        self.query.trace_position
    }
    pub fn allocation(&self) -> u64 {
        self.resolved.allocation
    }
    pub fn pool(&self) -> PoolDomain {
        self.resolved.pool
    }
    pub fn epoch(&self) -> u64 {
        self.resolved.epoch
    }
    pub fn offset(&self) -> u32 {
        self.resolved.offset
    }
    pub fn extent(&self) -> u32 {
        self.resolved.extent
    }
    pub fn checkpoint_position(&self) -> usize {
        self.resolved.checkpoint_position
    }
    pub fn same_capture(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.capture, &other.capture)
    }
    pub fn same_allocation(&self, other: &Self) -> crate::source_lifetimes::Evidence<bool> {
        use crate::source_lifetimes::{Evidence, GapKind, Reason};
        if !self.same_capture(other) {
            return Evidence {
                value: None,
                reasons: vec![Reason {
                    kind: GapKind::ForeignReceipt,
                    pool: Some(other.pool()),
                    allocation: Some(AllocationId(other.allocation())),
                    detail: "historical receipt belongs to another prepared capture".into(),
                }],
            };
        }
        Evidence {
            value: Some(
                self.allocation() == other.allocation()
                    && self.pool() == other.pool()
                    && self.epoch() == other.epoch(),
            ),
            reasons: vec![],
        }
    }
    pub fn same_span(&self, other: &Self) -> crate::source_lifetimes::Evidence<bool> {
        let mut evidence = self.same_allocation(other);
        if let Some(same) = evidence.value {
            evidence.value =
                Some(same && self.offset() == other.offset() && self.extent() == other.extent());
        }
        evidence
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct HistoricalResolution {
    pub reference: Option<HistoricalReference>,
    pub observations: Evaluation,
}
impl From<HistoricalResolution> for Resolution {
    fn from(value: HistoricalResolution) -> Self {
        Self {
            allocation: value.reference.map(|r| r.resolved),
            observations: value.observations,
        }
    }
}

struct PreparedData {
    events: Vec<Event>,
    intervals: Vec<CoverageInterval>,
    observations: Evaluation,
    invalid: Option<Evaluation>,
}
/// Immutable validated timeline. Clones share its private capture identity;
/// preparing the same raw History again deliberately creates a separate one.
/// Each batch replays one ledger in increasing position order and restores the
/// caller's original query order. No ledger or serialized coverage is trusted
/// from the public History display fields.
#[derive(Clone)]
pub struct PreparedHistory {
    data: Arc<PreparedData>,
    capture: Arc<()>,
}
fn hex(r: &Record, field: &str) -> Result<u32> {
    Ok(u32::from_str_radix(r.get(field)?, 16)?)
}
fn bit(r: &Record, field: &str) -> Result<bool> {
    let n: u8 = r.number(field)?;
    ensure!(n <= 1, "invalid {field} bit");
    Ok(n == 1)
}
fn epochs(r: &Record) -> Result<[u32; 5]> {
    Ok([
        r.number("epoch0")?,
        r.number("epoch1")?,
        r.number("epoch2")?,
        r.number("epoch3")?,
        r.number("epoch4")?,
    ])
}
fn domain(n: u8) -> Result<PoolDomain> {
    match n {
        0 => Ok(PoolDomain::Heap),
        1 => Ok(PoolDomain::Optimizer),
        2 => Ok(PoolDomain::Block),
        3 => Ok(PoolDomain::Local),
        4 => Ok(PoolDomain::Global),
        _ => Err(anyhow!("unsupported arena domain {n}")),
    }
}
fn pool_number(pool: PoolDomain) -> u8 {
    match pool {
        PoolDomain::Heap => 0,
        PoolDomain::Optimizer => 1,
        PoolDomain::Block => 2,
        PoolDomain::Local => 3,
        PoolDomain::Global => 4,
    }
}
fn pair_pool(pair: u8) -> Result<u8> {
    match pair {
        1 | 5..=9 => Ok(4),
        2 => Ok(3),
        3 => Ok(2),
        4 => Ok(1),
        _ => Err(anyhow!("unsupported arena allocation pair {pair}")),
    }
}
fn reset(r: &Record, begin: bool) -> Result<Reset> {
    Ok(Reset {
        reset: r.number("reset")?,
        parent_reset: r.number("parent_reset")?,
        op: r.number("op")?,
        mask: r.number("mask")?,
        skip: bit(r, "skip")?,
        request_depth: r.number("request_depth")?,
        depth: r.number("depth")?,
        epochs: epochs(r)?,
        lock_value: if begin {
            Some(r.number("lock_value")?)
        } else {
            None
        },
        status_known: if begin {
            None
        } else {
            Some(bit(r, "status_known")?)
        },
        status: if begin {
            None
        } else {
            Some(r.number("status")?)
        },
    })
}
fn parse_event(r: &Record) -> Result<Event> {
    let decoded = trace::records(&r.raw);
    ensure!(
        decoded.len() == 1 && decoded[0].kind == r.kind && decoded[0].fields == r.fields,
        "retained arena raw record disagrees with decoded display"
    );
    let mut keys = std::collections::BTreeSet::new();
    for token in r.raw.split_whitespace().skip(1) {
        let (key, value) = token
            .split_once('=')
            .ok_or_else(|| anyhow!("invalid arena field token"))?;
        ensure!(
            !value.is_empty() && keys.insert(key),
            "duplicate or empty arena field"
        );
    }
    let fields: &[&str] = match r.kind.as_str() {
        "pv.arena.begin" => &["scope", "version", "pools", "lock"],
        "pv.arena.request" => &[
            "request",
            "parent_request",
            "pair",
            "pool",
            "requested",
            "retail_esp",
            "site",
            "epoch",
            "depth",
        ],
        "pv.arena.alloc" => &[
            "allocation",
            "request",
            "pair",
            "pool",
            "requested",
            "requested_known",
            "reserved",
            "address",
            "block",
            "freep",
            "free",
            "retail_esp",
            "site",
            "epoch",
            "complete",
        ],
        "pv.arena.reset.begin" => &[
            "reset",
            "parent_reset",
            "op",
            "mask",
            "lock_value",
            "skip",
            "request_depth",
            "depth",
            "epoch0",
            "epoch1",
            "epoch2",
            "epoch3",
            "epoch4",
        ],
        "pv.arena.reset.end" => &[
            "reset",
            "parent_reset",
            "op",
            "mask",
            "skip",
            "status_known",
            "status",
            "request_depth",
            "depth",
            "epoch0",
            "epoch1",
            "epoch2",
            "epoch3",
            "epoch4",
        ],
        "pv.arena.gap" => &["reason", "mask", "site"],
        "pv.arena.checkpoint" => &[
            "checkpoints",
            "requests",
            "allocations",
            "resets",
            "reset_ends",
            "gaps",
            "request_depth",
            "reset_depth",
            "unsupported",
            "function",
            "epoch0",
            "epoch1",
            "epoch2",
            "epoch3",
            "epoch4",
        ],
        _ => return Err(anyhow!("unknown arena record {}", r.kind)),
    };
    ensure!(
        r.fields
            .keys()
            .all(|k| matches!(k.as_str(), "schema" | "stream" | "event")
                || fields.contains(&k.as_str())),
        "unknown arena observation field"
    );
    ensure!(
        r.number::<u8>("schema")? == 1 && r.number::<u8>("stream")? == 1,
        "unsupported arena schema or stream"
    );
    let event = r.number("event")?;
    let observation = match r.kind.as_str() {
        "pv.arena.begin" => {
            ensure!(r.get("scope")? == SCOPE, "unsupported arena scope");
            let version = r.number("version")?;
            let pools = hex(r, "pools")?;
            let lock = hex(r, "lock")?;
            ensure!(
                (1..=4).contains(&version) && pools != 0 && lock != 0,
                "invalid arena header"
            );
            EventKind::Begin {
                version,
                pools,
                lock,
            }
        }
        "pv.arena.request" => EventKind::Request(Request {
            request: r.number("request")?,
            parent_request: r.number("parent_request")?,
            pair: r.number("pair")?,
            pool: r.number("pool")?,
            requested: r.number("requested")?,
            retail_esp: hex(r, "retail_esp")?,
            site: hex(r, "site")?,
            epoch: r.number("epoch")?,
            depth: r.number("depth")?,
        }),
        "pv.arena.alloc" => EventKind::Allocation(Commit {
            allocation: r.number("allocation")?,
            request: r.number("request")?,
            pair: r.number("pair")?,
            pool: r.number("pool")?,
            requested: r.number("requested")?,
            requested_known: bit(r, "requested_known")?,
            reserved: r.number("reserved")?,
            address: hex(r, "address")?,
            block: hex(r, "block")?,
            freep: hex(r, "freep")?,
            free: r.number("free")?,
            retail_esp: hex(r, "retail_esp")?,
            site: hex(r, "site")?,
            epoch: r.number("epoch")?,
            complete: bit(r, "complete")?,
        }),
        "pv.arena.reset.begin" => EventKind::ResetBegin(reset(r, true)?),
        "pv.arena.reset.end" => EventKind::ResetEnd(reset(r, false)?),
        "pv.arena.gap" => EventKind::Gap {
            reason: r.get("reason")?.into(),
            mask: r.number("mask")?,
            site: hex(r, "site")?,
        },
        "pv.arena.checkpoint" => EventKind::Checkpoint(Checkpoint {
            trace_position: r.line,
            event,
            totals: Totals {
                checkpoints: r.number("checkpoints")?,
                requests: r.number("requests")?,
                allocations: r.number("allocations")?,
                resets: r.number("resets")?,
                reset_ends: r.number("reset_ends")?,
                gaps: r.number("gaps")?,
            },
            request_depth: r.number("request_depth")?,
            reset_depth: r.number("reset_depth")?,
            unsupported: bit(r, "unsupported")?,
            function: r.number("function")?,
            epochs: epochs(r)?,
            observations: known(),
        }),
        _ => return Err(anyhow!("unknown arena record {}", r.kind)),
    };
    Ok(Event {
        trace_position: r.line,
        event,
        observation,
    })
}

/// Parse only this family while preserving original one-based trace positions.
pub fn parse(text: &str) -> Result<History> {
    if !text.contains("pv.arena.") {
        return Ok(History::default());
    }
    let mut after_checkpoint = false;
    let records = text
        .lines()
        .enumerate()
        .filter_map(|(i, line)| {
            let kind = trace::record_kind(line)?;
            let include = after_checkpoint
                || is_record(kind)
                || matches!(kind, "pv.function.begin" | "pv.function.end");
            after_checkpoint = kind == "pv.arena.checkpoint";
            if !include {
                return None;
            }
            let mut r = trace::records(line).pop()?;
            r.line = i + 1;
            Some(r)
        })
        .take(MAX_EVENTS + 1)
        .collect();
    Ok(validate(records))
}

struct Validator {
    events: Vec<Event>,
    checkpoints: Vec<Checkpoint>,
    intervals: Vec<CoverageInterval>,
    requests: Vec<Request>,
    resets: Vec<Reset>,
    totals: Totals,
    epochs: [u32; 5],
    version: Option<u8>,
    first_position: usize,
    first_gap: Option<usize>,
    last_position: usize,
}
impl Validator {
    fn new() -> Self {
        Self {
            events: vec![],
            checkpoints: vec![],
            intervals: vec![],
            requests: vec![],
            resets: vec![],
            totals: Totals::default(),
            epochs: [0; 5],
            version: None,
            first_position: 0,
            first_gap: None,
            last_position: 0,
        }
    }
    fn accept(&mut self, e: &Event) -> Result<()> {
        ensure!(
            e.trace_position > self.last_position,
            "arena trace positions reordered"
        );
        ensure!(
            e.event as usize == self.events.len() + 1,
            "arena event stream is missing or reordered"
        );
        ensure!(
            self.version.is_some() || matches!(e.observation, EventKind::Begin { .. }),
            "arena record before begin"
        );
        match &e.observation {
            EventKind::Begin { version, .. } => {
                ensure!(
                    self.version.is_none() && e.event == 1,
                    "duplicate arena begin"
                );
                self.version = Some(*version);
                self.first_position = e.trace_position;
            }
            EventKind::Request(r) => {
                ensure!(
                    r.request
                        == self
                            .totals
                            .requests
                            .checked_add(1)
                            .ok_or_else(|| anyhow!("request count overflow"))?,
                    "arena request id mismatch"
                );
                ensure!(
                    pair_pool(r.pair)? == r.pool && r.epoch == self.epochs[r.pool as usize],
                    "arena request pool/epoch mismatch"
                );
                ensure!(
                    r.parent_request == self.requests.last().map_or(0, |p| p.request)
                        && r.depth as usize == self.requests.len() + 1
                        && self.requests.len() < MAX_DEPTH,
                    "arena request nesting mismatch"
                );
                ensure!(
                    r.retail_esp != 0 && r.site != 0,
                    "missing arena request ABI identity"
                );
                ensure!(
                    crate::source_arena_probe::valid_site(
                        self.version.unwrap(),
                        r.pair,
                        false,
                        r.site
                    ),
                    "unguarded arena request site"
                );
                self.totals.requests += 1;
                self.requests.push(r.clone());
            }
            EventKind::Allocation(a) => {
                ensure!(
                    a.allocation
                        == self
                            .totals
                            .allocations
                            .checked_add(1)
                            .ok_or_else(|| anyhow!("allocation count overflow"))?,
                    "arena allocation id mismatch"
                );
                ensure!(
                    pair_pool(a.pair)? == a.pool && a.epoch == self.epochs[a.pool as usize],
                    "arena allocation pool/epoch mismatch"
                );
                ensure!(
                    crate::source_arena_probe::valid_site(
                        self.version.unwrap(),
                        a.pair,
                        true,
                        a.site
                    ),
                    "unguarded arena commit site"
                );
                self.totals.allocations += 1;
                if a.complete {
                    let r = self
                        .requests
                        .last()
                        .ok_or_else(|| anyhow!("arena commit without request"))?;
                    ensure!(
                        a.request == r.request
                            && a.pair == r.pair
                            && a.pool == r.pool
                            && a.retail_esp == r.retail_esp
                            && a.epoch == r.epoch
                            && a.requested_known
                            && a.requested == r.requested,
                        "arena request/commit mismatch"
                    );
                    let rounded = (a.requested & !7)
                        .checked_add(8)
                        .ok_or_else(|| anyhow!("arena rounded request overflow"))?;
                    ensure!(
                        rounded <= i32::MAX as u32
                            && rounded == a.reserved
                            && a.address != 0
                            && a.block != 0
                            && a.free >= 0
                            && a.address.checked_add(a.reserved) == Some(a.freep),
                        "invalid arena committed span"
                    );
                    ensure!(a.site != 0, "missing arena commit site");
                    self.requests.pop();
                } else {
                    self.first_gap.get_or_insert(e.trace_position);
                    // The observer only pops an exactly matching frame, even
                    // when sticky unsupported coverage makes complete=false.
                    if self.requests.last().is_some_and(|r| {
                        a.request == r.request
                            && a.pair == r.pair
                            && a.pool == r.pool
                            && a.retail_esp == r.retail_esp
                            && a.epoch == r.epoch
                    }) {
                        self.requests.pop();
                    }
                }
            }
            EventKind::ResetBegin(r) => {
                ensure!(
                    r.reset
                        == self
                            .totals
                            .resets
                            .checked_add(1)
                            .ok_or_else(|| anyhow!("reset count overflow"))?,
                    "arena reset id mismatch"
                );
                let mask = match r.op {
                    1 => 2,
                    2 => 4,
                    3 => 8,
                    4 | 5 => 16,
                    6..=8 => 31,
                    _ => return Err(anyhow!("unsupported arena reset operation")),
                };
                ensure!(
                    r.mask == mask && r.skip == (r.op == 3 && r.lock_value.unwrap() != 0),
                    "arena reset mask/lock mismatch"
                );
                ensure!(
                    r.request_depth as usize == self.requests.len()
                        && r.parent_reset == self.resets.last().map_or(0, |p| p.reset)
                        && r.depth as usize == self.resets.len() + 1
                        && self.resets.len() < MAX_DEPTH,
                    "arena reset nesting mismatch"
                );
                for n in 0..5 {
                    if !r.skip && mask & (1 << n) != 0 {
                        self.epochs[n] = self.epochs[n]
                            .checked_add(1)
                            .ok_or_else(|| anyhow!("arena epoch overflow"))?;
                    }
                }
                ensure!(r.epochs == self.epochs, "arena reset epoch mismatch");
                self.totals.resets += 1;
                self.resets.push(r.clone());
            }
            EventKind::ResetEnd(r) => {
                let begin = self
                    .resets
                    .last()
                    .ok_or_else(|| anyhow!("arena reset end without begin"))?;
                ensure!(
                    r.reset == begin.reset
                        && r.parent_reset == begin.parent_reset
                        && r.op == begin.op
                        && r.mask == begin.mask
                        && r.skip == begin.skip
                        && r.depth as usize + 1 == self.resets.len()
                        && r.request_depth as usize == self.requests.len()
                        && r.epochs == self.epochs,
                    "arena reset end mismatch"
                );
                ensure!(
                    r.status_known == Some(matches!(r.op, 7 | 8)),
                    "arena reset status scope mismatch"
                );
                if r.status_known == Some(true) {
                    ensure!(
                        matches!(r.status, Some(0 | 0xffff_ffff)),
                        "unknown arena init status"
                    );
                    if r.status != Some(0) {
                        self.first_gap.get_or_insert(e.trace_position);
                    }
                }
                self.totals.reset_ends += 1;
                self.resets.pop();
            }
            EventKind::Gap { reason, mask, site } => {
                ensure!(*mask == 31, "unsupported arena gap mask");
                ensure!(
                    reason != "allocator_callback"
                        || crate::source_arena_probe::valid_callback_site(
                            self.version.unwrap(),
                            *site
                        ),
                    "unguarded arena callback site"
                );
                self.totals.gaps = self
                    .totals
                    .gaps
                    .checked_add(1)
                    .ok_or_else(|| anyhow!("gap count overflow"))?;
                self.first_gap.get_or_insert(e.trace_position);
            }
            EventKind::Checkpoint(c) => {
                self.totals.checkpoints = self
                    .totals
                    .checkpoints
                    .checked_add(1)
                    .ok_or_else(|| anyhow!("checkpoint count overflow"))?;
                ensure!(
                    c.totals == self.totals
                        && c.request_depth as usize == self.requests.len()
                        && c.reset_depth as usize == self.resets.len()
                        && c.epochs == self.epochs,
                    "arena checkpoint totals/depth/epoch mismatch"
                );
                ensure!(
                    !c.unsupported || self.first_gap.is_some(),
                    "unlocalized unsupported arena coverage"
                );
                ensure!(
                    c.unsupported || self.first_gap.is_none(),
                    "arena unsupported state was cleared"
                );
                let mut c = c.clone();
                if !self.requests.is_empty() || !self.resets.is_empty() {
                    c.observations = gap("arena checkpoint has active requests or resets");
                } else {
                    // Counters were sampled before checkpoint formatting.
                    // Formatting can expose a later reentrant observer loss.
                    let end = self
                        .first_gap
                        .map_or(e.trace_position.saturating_sub(1), |p| p.saturating_sub(1));
                    if end >= self.first_position {
                        self.intervals.push(CoverageInterval {
                            begin_position: self.first_position,
                            end_position: end,
                            checkpoint_position: e.trace_position,
                        });
                    }
                    if self.first_gap.is_some() {
                        c.observations = gap(
                            "arena checkpoint reconciles only the prefix before unsupported observations",
                        );
                    }
                }
                self.checkpoints.push(c);
            }
        }
        self.last_position = e.trace_position;
        self.events.push(e.clone());
        Ok(())
    }
}
fn validate(records: Vec<Record>) -> History {
    let mut validator = Validator::new();
    let mut failure = None;
    let mut backend_stack = vec![];
    let mut backend_ids = std::collections::BTreeSet::new();
    let mut backend_ambiguous = false;
    let mut last_position = 0;
    for (index, record) in records.iter().enumerate() {
        if validator.events.len() == MAX_EVENTS {
            failure = Some("arena event limit exceeded".into());
            break;
        }
        let result = (|| -> Result<()> {
            ensure!(
                record.line > last_position,
                "retained trace records reordered"
            );
            let raw = trace::records(&record.raw);
            ensure!(
                raw.len() == 1 && raw[0].kind == record.kind && raw[0].fields == record.fields,
                "retained raw record disagrees with decoded display"
            );
            match record.kind.as_str() {
                "pv.function.begin" => {
                    let function = record.number::<u32>("function")?;
                    if function == 0
                        || !backend_ids.insert(function)
                        || backend_stack.len() == MAX_DEPTH
                    {
                        backend_ambiguous = true;
                    }
                    backend_stack.push(function);
                    return Ok(());
                }
                "pv.function.end" => {
                    let function = record.number::<u32>("function")?;
                    if backend_stack.pop() != Some(function) {
                        backend_ambiguous = true;
                    }
                    return Ok(());
                }
                _ if !is_record(&record.kind) => return Ok(()),
                _ => {}
            }
            let event = parse_event(record)?;
            if let EventKind::Checkpoint(c) = &event.observation {
                if c.function == 0 {
                    ensure!(
                        validator
                            .events
                            .last()
                            .is_some_and(|e| matches!(e.observation, EventKind::ResetEnd(_))),
                        "arena zero-function checkpoint is not after reset end"
                    );
                } else {
                    ensure!(
                        !backend_ambiguous && backend_stack.last() == Some(&c.function),
                        "arena checkpoint has no exact backend function scope"
                    );
                    let end = records.get(index + 1).ok_or_else(|| {
                        anyhow!("arena function checkpoint missing independent function end")
                    })?;
                    let end_raw = trace::records(&end.raw);
                    ensure!(
                        end.line > record.line
                            && end_raw.len() == 1
                            && end_raw[0].kind == end.kind
                            && end_raw[0].fields == end.fields,
                        "arena checkpoint function-end raw evidence disagrees"
                    );
                    ensure!(
                        end.kind == "pv.function.end"
                            && end.number::<u32>("function")? == c.function,
                        "arena checkpoint is not immediately followed by its function end"
                    );
                }
            }
            validator.accept(&event)
        })();
        last_position = record.line;
        if let Err(error) = result {
            failure = Some(format!("arena trace line {}: {error:#}", record.line));
            break;
        }
    }
    let observations = if let Some(failure) = failure {
        gap(failure)
    } else if validator.version.is_none() {
        gap("no guarded arena stream")
    } else if validator
        .events
        .last()
        .is_none_or(|e| !matches!(e.observation, EventKind::Checkpoint(_)))
    {
        gap("arena stream has an uncheckpointed suffix")
    } else if validator.first_gap.is_some() {
        gap("arena stream contains unsupported observations")
    } else if validator
        .checkpoints
        .last()
        .is_some_and(|c| c.observations.state != EvaluationState::Satisfied)
    {
        gap("arena stream ends with an unbalanced checkpoint")
    } else if validator.intervals.is_empty() {
        gap("arena stream has no balanced reconciled checkpoint")
    } else {
        known()
    };
    History {
        format_version: 1,
        scope: validator.version.map(|_| SCOPE.into()),
        events: validator.events,
        checkpoints: validator.checkpoints,
        coverage_intervals: validator.intervals,
        observations,
        records,
    }
}
impl History {
    /// Revalidate the authoritative raw sequence once. The resulting capture is
    /// immutable and remains independent from later edits to this display model.
    pub fn prepare(&self) -> PreparedHistory {
        let data = if self.records.len() > MAX_EVENTS {
            let error = gap("arena event limit exceeded");
            PreparedData {
                events: vec![],
                intervals: vec![],
                observations: error.clone(),
                invalid: Some(error),
            }
        } else {
            let rebuilt = validate(self.records.clone());
            PreparedData {
                events: rebuilt.events,
                intervals: rebuilt.coverage_intervals,
                observations: rebuilt.observations,
                invalid: None,
            }
        };
        PreparedHistory {
            data: Arc::new(data),
            capture: Arc::new(()),
        }
    }

    pub fn resolve_at(
        &self,
        trace_position: usize,
        pool: PoolDomain,
        address: u32,
        extent: u32,
    ) -> Resolution {
        self.prepare()
            .resolve_many(&[Query {
                trace_position,
                pool,
                address,
                extent,
            }])
            .pop()
            .unwrap()
    }
}

impl PreparedData {
    fn checkpoint(&self, position: usize) -> std::result::Result<usize, Evaluation> {
        if let Some(error) = &self.invalid {
            return Err(error.clone());
        }
        if let Ok(index) = self
            .events
            .binary_search_by_key(&position, |e| e.trace_position)
            && matches!(self.events[index].observation, EventKind::Checkpoint(_))
        {
            return Err(gap(
                "checkpoint emission is a coverage boundary, not an allocation observation position",
            ));
        }
        // Validated prefix ends are monotonic, including repeated ends after a
        // located gap. Select the earliest independent checkpoint covering it.
        let index = self
            .intervals
            .partition_point(|interval| interval.end_position < position);
        self.intervals
            .get(index)
            .filter(|i| i.begin_position <= position)
            .map(|i| i.checkpoint_position)
            .ok_or_else(|| {
                gap("requested arena position is outside a reconciled checkpoint prefix")
            })
    }
}

struct Cursor {
    ledger: Ledger,
    seen: [bool; 5],
    epochs: [u32; 5],
    reset_depth: u32,
    next_event: usize,
}
impl Cursor {
    fn new() -> Self {
        Self {
            ledger: Ledger::new(MAX_EVENTS),
            seen: [false; 5],
            epochs: [0; 5],
            reset_depth: 0,
            next_event: 0,
        }
    }
    fn advance(&mut self, events: &[Event], position: usize) {
        while let Some(e) = events
            .get(self.next_event)
            .filter(|e| e.trace_position <= position)
        {
            match &e.observation {
                EventKind::ResetBegin(r) => {
                    self.reset_depth += 1;
                    if r.skip {
                        let _ = self
                            .ledger
                            .observe_locked_local_reset(u64::from(self.epochs[3]));
                    } else {
                        for n in 0..5 {
                            if r.mask & (1 << n) != 0 {
                                let _ = self.ledger.observe_invalidation(
                                    domain(n as u8).unwrap(),
                                    self.seen[n].then_some(u64::from(self.epochs[n])),
                                    u64::from(r.epochs[n]),
                                    if matches!(r.op, 5 | 6) {
                                        InvalidationKind::Release
                                    } else {
                                        InvalidationKind::Reset
                                    },
                                    Coverage::COMPLETE,
                                );
                                self.seen[n] = true;
                            }
                        }
                    }
                    self.epochs = r.epochs;
                }
                EventKind::ResetEnd(_) => self.reset_depth -= 1,
                EventKind::Allocation(a) => {
                    let _ = self.ledger.observe_allocation(Allocation {
                        id: AllocationId(u64::from(a.allocation)),
                        pool: domain(a.pool).unwrap(),
                        epoch: u64::from(a.epoch),
                        address: a.address,
                        requested: a.requested,
                        reserved: a.reserved,
                    });
                }
                _ => {}
            }
            self.next_event += 1;
        }
    }
}
impl PreparedHistory {
    /// Whole-stream status; valid earlier prefixes may exist even when a later
    /// unsupported or truncated suffix makes this status indeterminate.
    pub fn observations(&self) -> &Evaluation {
        &self.data.observations
    }

    pub fn resolve_many(&self, queries: &[Query]) -> Vec<Resolution> {
        self.resolve_references(queries)
            .into_iter()
            .map(Into::into)
            .collect()
    }

    pub fn resolve_references(&self, queries: &[Query]) -> Vec<HistoricalResolution> {
        let mut order = (0..queries.len()).collect::<Vec<_>>();
        order.sort_unstable_by_key(|&index| (queries[index].trace_position, index));
        let mut results = (0..queries.len()).map(|_| None).collect::<Vec<_>>();
        let mut cursor = Cursor::new();
        for index in order {
            let query = queries[index];
            let result = match self.data.checkpoint(query.trace_position) {
                Err(observations) => HistoricalResolution {
                    reference: None,
                    observations,
                },
                Ok(checkpoint_position) => {
                    cursor.advance(&self.data.events, query.trace_position);
                    if cursor.reset_depth != 0 {
                        HistoricalResolution {
                            reference: None,
                            observations: gap("requested arena position is inside an active reset"),
                        }
                    } else {
                        let receipt = cursor.ledger.resolve(
                            query.pool,
                            u64::from(cursor.epochs[pool_number(query.pool) as usize]),
                            query.address,
                            query.extent,
                        );
                        match receipt.value {
                            Some(r) => HistoricalResolution {
                                reference: Some(HistoricalReference {
                                    capture: self.capture.clone(),
                                    query,
                                    resolved: ResolvedAllocation {
                                        allocation: r.allocation().0,
                                        pool: r.pool(),
                                        epoch: r.epoch(),
                                        offset: r.offset(),
                                        extent: r.extent(),
                                        trace_position: query.trace_position,
                                        checkpoint_position,
                                    },
                                }),
                                observations: known(),
                            },
                            None => HistoricalResolution {
                                reference: None,
                                observations: gap(format!(
                                    "allocation lifetime is indeterminate: {:?}",
                                    receipt.reasons
                                )),
                            },
                        }
                    }
                }
            };
            results[index] = Some(result);
        }
        results.into_iter().map(Option::unwrap).collect()
    }
}

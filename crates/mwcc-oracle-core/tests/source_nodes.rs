use mwcc_oracle_core::{evidence::EvaluationState, source_nodes};

#[derive(Default)]
struct Stream {
    lines: Vec<String>,
    source_event: usize,
    arena_event: usize,
    allocations: u32,
    snapshots: u32,
    emissions: u32,
    clones: u32,
}
impl Stream {
    fn new() -> Self {
        let mut s = Self::default();
        s.arena("begin scope=guarded_retail_arenas_v1 version=3 pools=100006 lock=100000");
        s.arena("reset.begin reset=1 parent_reset=0 op=7 mask=31 lock_value=0 skip=0 request_depth=0 depth=1 epoch0=1 epoch1=1 epoch2=1 epoch3=1 epoch4=1");
        s.arena("reset.end reset=1 parent_reset=0 op=7 mask=31 skip=0 status_known=1 status=0 request_depth=0 depth=0 epoch0=1 epoch1=1 epoch2=1 epoch3=1 epoch4=1");
        s.source("begin scope=guarded_source_nodes_v1 version=3");
        for (address, size) in [
            (0x400000, 1),
            (0x400100, 30),
            (0x400200, 30),
            (0x400240, 30),
            (0x400280, 30),
            (0x4002c0, 1),
        ] {
            s.allocate(address, size);
        }
        s
    }
    fn source(&mut self, fields: &str) {
        self.source_event += 1;
        let (kind, fields) = fields.split_once(' ').unwrap();
        self.lines.push(format!(
            "pv.source.{kind} schema=1 stream=1 event={} {fields}",
            self.source_event
        ));
    }
    fn arena(&mut self, fields: &str) {
        self.arena_event += 1;
        let (kind, fields) = fields.split_once(' ').unwrap();
        self.lines.push(format!(
            "pv.arena.{kind} schema=1 stream=1 event={} {fields}",
            self.arena_event
        ));
    }
    fn allocate(&mut self, address: u32, requested: u32) {
        self.allocations += 1;
        let id = self.allocations;
        let reserved = (requested & !7) + 8;
        self.arena(&format!("request request={id} parent_request=0 pair=2 pool=3 requested={requested} retail_esp=200000 site=440d25 epoch=1 depth=1"));
        self.arena(&format!("alloc allocation={id} request={id} pair=2 pool=3 requested={requested} requested_known=1 reserved={reserved} address={address:x} block=300000 freep={:x} free=1024 retail_esp=200000 site=440d40 epoch=1 complete=1",address+reserved));
    }
    fn owner_begin(&mut self) {
        self.source("owner.begin owner=1 parent_owner=0 function=400000 depth=1");
    }
    fn owner_end(&mut self) {
        self.source("owner.end owner=1 parent_owner=0 function=400000 depth=0");
    }
    // Keep the wire record's independently varied fields explicit in fixtures.
    #[allow(clippy::too_many_arguments)]
    fn node(
        &mut self,
        frame: u32,
        stage: u8,
        address: u32,
        parent: u32,
        edge: u8,
        tag: u8,
        shape: u8,
        data: [u32; 4],
    ) -> u32 {
        self.snapshots += 1;
        let id = self.snapshots;
        self.source(&format!("node snapshot={id} frame={frame} stage={stage} node={address:x} parent={parent} edge={edge} tag_known=1 tag={tag} shape={shape} flags=0 type=0 data0={:x} data1={:x} data2={:x} data3={:x} complete=1 reason=ok",data[0],data[1],data[2],data[3]));
        id
    }
    fn assignment(&mut self) -> u32 {
        self.emissions += 1;
        let id = self.emissions;
        let fields = format!(
            "emission={id} owner=1 function=400000 current_function=400000 statement=400100 root=400200 site=4f360e"
        );
        self.source(&format!("emission.begin {fields} depth=1"));
        let root = self.node(id, 1, 0x400200, 0, 0, 30, 2, [0x400240, 0x400280, 0, 0]);
        self.node(id, 1, 0x400240, root, 1, 56, 4, [0x4002c0, 0, 0, 0]);
        self.node(id, 1, 0x400280, root, 2, 50, 5, [0, 7, 0, 0]);
        self.source(&format!("emission.end {fields} depth=0 snapshot={root}"));
        root
    }
    fn clone_call(&mut self, owner: u32) -> [u32; 3] {
        self.clones += 1;
        let id = self.clones;
        self.source(&format!("clone.begin invocation={id} parent_clone=0 owner={owner} mode_scope=0 input=400280 caller=4f0000 eval_mode=0 alloc_state=0 depth=1"));
        let mut snapshots = [0; 3];
        for (index, stage) in (2..=4).enumerate() {
            snapshots[index] = self.node(id, stage, 0x400280, 0, 0, 50, 5, [0, index as u32, 0, 0]);
        }
        self.source(&format!("clone.end invocation={id} parent_clone=0 owner={owner} mode_scope=0 input=400280 output=400280 eval_mode=0 alloc_state=0 depth=0 input_snapshot={} after_snapshot={} output_snapshot={}",snapshots[0],snapshots[1],snapshots[2]));
        snapshots
    }
    fn checkpoint(&mut self, owners: u32) {
        self.lines.push("pv.function.begin function=1".into());
        self.source(&format!("checkpoint checkpoints=1 owners={owners} owner_ends={owners} modes=0 mode_ends=0 clones={} clone_ends={} emissions={} emission_ends={} snapshots={} gaps=0 owner_depth=0 mode_depth=0 clone_depth=0 emission_depth=0 unsupported=0",self.clones,self.clones,self.emissions,self.emissions,self.snapshots));
        self.arena(&format!("checkpoint checkpoints=1 requests={} allocations={} resets=1 reset_ends=1 gaps=0 request_depth=0 reset_depth=0 unsupported=0 function=1 epoch0=1 epoch1=1 epoch2=1 epoch3=1 epoch4=1",self.allocations,self.allocations));
        self.lines.push("pv.function.end function=1".into());
    }
    fn text(&self) -> String {
        self.lines.join("\n")
    }
}
fn assignment() -> Stream {
    let mut s = Stream::new();
    s.owner_begin();
    s.assignment();
    s.owner_end();
    s.checkpoint(1);
    s
}
fn prepared(text: &str) -> source_nodes::PreparedNodes {
    source_nodes::parse(text).unwrap().prepare()
}

#[test]
fn assignment_receipts_require_the_observed_tree_and_allocation_closure() {
    let s = assignment();
    let p = prepared(&s.text());
    let answer = p.source_write(1);
    assert_eq!(
        answer.observations.state,
        EvaluationState::Satisfied,
        "{answer:?}"
    );
    let write = answer.value.unwrap();
    assert_eq!(write.root().observation().tag, 30);
    assert_eq!(write.lhs().observation().data[0], 0x4002c0);
    assert_eq!(write.rhs().observation().data[1], 7);
    assert_eq!(write.root().allocation().extent(), 30);
    assert_eq!(write.statement().extent(), 30);
    assert_eq!(write.owner().extent(), 1);
    // The Object leaf exposes a pointer, not the unobserved tagged offset.
    assert_eq!(write.lhs().observation().data, [0x4002c0, 0, 0, 0]);
    for (old, new) in [
        ("current_function=400000", "current_function=400001"),
        (
            "requested=30 requested_known=1",
            "requested=29 requested_known=1",
        ),
        ("data0=4002c0", "data0=4002c1"),
    ] {
        assert!(
            prepared(&s.text().replace(old, new))
                .source_write(1)
                .value
                .is_none(),
            "accepted {old} -> {new}"
        );
    }
}

#[test]
fn actual_clone_reads_are_distinct_even_when_allocation_and_pointer_are_the_same() {
    let mut s = Stream::new();
    let ids = s.clone_call(0);
    s.checkpoint(0);
    let p = prepared(&s.text());
    let clone = p.clone_edge(1).value.unwrap();
    assert!(clone.owner().is_none());
    assert_eq!(
        clone.owner_observations().state,
        EvaluationState::Indeterminate
    );
    let input = clone.input().unwrap();
    let after = clone.after().unwrap();
    assert_eq!(input.snapshot(), ids[0]);
    assert_eq!(input.same_observation(after).value, Some(false));
    assert_eq!(input.same_allocation(after).value, Some(true));
    assert_ne!(input.observation().data, after.observation().data);
    let foreign = prepared(&s.text()).snapshot(ids[0]).value.unwrap();
    assert!(input.same_observation(&foreign).value.is_none());
    assert!(input.same_allocation(&foreign).value.is_none());
    assert_eq!(
        input
            .same_observation(p.clone().snapshot(ids[0]).value.as_ref().unwrap())
            .value,
        Some(true)
    );
}

#[test]
fn raw_records_are_authoritative_and_display_summaries_cannot_forge_receipts() {
    let s = assignment();
    let mut h = source_nodes::parse(&s.text()).unwrap();
    h.nodes[0].tag = 50;
    h.emissions.clear();
    h.coverage_intervals.clear();
    assert!(h.prepare().source_write(1).value.is_some());
    h.records[0].line += 1;
    assert!(h.prepare().source_write(1).value.is_none());
    let mut h = source_nodes::parse(&s.text()).unwrap();
    h.trace = h
        .trace
        .replace("current_function=400000", "current_function=400001");
    assert!(h.prepare().source_write(1).value.is_none());
    let encoded = serde_json::to_string(&source_nodes::parse(&s.text()).unwrap()).unwrap();
    let decoded: source_nodes::History = serde_json::from_str(&encoded).unwrap();
    assert!(decoded.prepare().source_write(1).value.is_some());
}

#[test]
fn lost_reordered_unknown_or_unreconciled_source_records_are_indeterminate() {
    let s = assignment();
    for remove in 0..s.lines.len() {
        if !s.lines[remove].starts_with("pv.source.") {
            continue;
        }
        let mut lines = s.lines.clone();
        lines.remove(remove);
        assert!(
            prepared(&lines.join("\n")).source_write(1).value.is_none(),
            "removed line {remove}"
        );
    }
    for (old, new) in [
        ("tag=30 shape=2", "tag=30 shape=1"),
        ("parent=1 edge=2", "parent=1 edge=1"),
        ("snapshots=3 gaps=0", "snapshots=4 gaps=0"),
        ("schema=1 stream=1", "schema=2 stream=1"),
        ("reason=ok", "reason=ok extra=1"),
        ("event=2 owner=1", "event=1 owner=1"),
        ("site=4f360e", "site=4f360f"),
    ] {
        assert!(
            prepared(&s.text().replace(old, new))
                .source_write(1)
                .value
                .is_none(),
            "accepted {old} -> {new}"
        );
    }
    let mut lines = s.lines.clone();
    lines.retain(|l| !l.starts_with("pv.source.checkpoint"));
    assert!(prepared(&lines.join("\n")).snapshot(1).value.is_none());
    let text = s.text().replace(
        "pv.arena.checkpoint schema=1",
        "pv.arena.checkpoint schema=2",
    );
    assert!(prepared(&text).snapshot(1).value.is_none());
}

#[test]
fn unsupported_subtree_is_visible_but_never_a_supported_tree_receipt() {
    let s = assignment();
    let text=s.text().replace("tag_known=1 tag=50 shape=5 flags=0 type=0 data0=0 data1=7 data2=0 data3=0 complete=1 reason=ok","tag_known=1 tag=55 shape=0 flags=0 type=0 data0=0 data1=0 data2=0 data3=0 complete=0 reason=unsupported");
    let p = prepared(&text);
    assert_eq!(
        p.observations().state,
        EvaluationState::Satisfied,
        "{:?}",
        p.observations()
    );
    assert_eq!(p.history().nodes[2].reason, "unsupported");
    assert!(p.snapshot(3).value.is_none());
    assert!(p.snapshot(1).value.is_none());
    assert!(p.source_write(1).value.is_none());
    assert!(p.snapshot(2).value.is_some());
}

#[test]
fn absent_stream_retains_no_trace_and_snapshot_zero_is_unknown() {
    let h = source_nodes::parse("pv.function.begin function=1\n").unwrap();
    assert!(h.trace.is_empty());
    assert!(h.records.is_empty());
    assert!(h.prepare().snapshot(0).value.is_none());
}

#[test]
fn actual_nested_clone_parents_and_raw_mode_argument_are_preserved() {
    let mut s = Stream::new();
    s.source("mode.begin invocation=1 parent_mode=0 owner=0 input=0 mode_word=258 eval_mode=0 alloc_state=0 depth=1");
    s.source("clone.begin invocation=1 parent_clone=0 owner=0 mode_scope=1 input=0 caller=4f0000 eval_mode=2 alloc_state=1 depth=1");
    s.source("clone.begin invocation=2 parent_clone=1 owner=0 mode_scope=1 input=0 caller=4f0010 eval_mode=2 alloc_state=1 depth=2");
    s.source("clone.end invocation=2 parent_clone=1 owner=0 mode_scope=1 input=0 output=0 eval_mode=2 alloc_state=1 depth=1 input_snapshot=0 after_snapshot=0 output_snapshot=0");
    s.source("clone.end invocation=1 parent_clone=0 owner=0 mode_scope=1 input=0 output=0 eval_mode=2 alloc_state=1 depth=0 input_snapshot=0 after_snapshot=0 output_snapshot=0");
    s.source("mode.end invocation=1 parent_mode=0 owner=0 input=0 output=0 eval_mode=3 alloc_state=0 depth=0 input_snapshot=0 after_snapshot=0 output_snapshot=0");
    s.clones = 2;
    s.checkpoint(0);
    let text = s
        .text()
        .replace("modes=0 mode_ends=0", "modes=1 mode_ends=1");
    let p = prepared(&text);
    assert_eq!(p.history().modes[0].mode_word, Some(258));
    assert_eq!(p.history().modes[0].end_eval_mode, Some(3));
    let edge = p.clone_edge(2).value.unwrap();
    assert_eq!(edge.parent_clone(), 1);
    assert_eq!(edge.mode_scope(), 1);
    assert!(edge.input().is_none());
    assert!(
        prepared(&text.replace("mode_word=258", "mode_word=261"))
            .clone_edge(2)
            .value
            .is_none()
    );
    assert!(
        prepared(&text.replace("parent_clone=1", "parent_clone=0"))
            .clone_edge(2)
            .value
            .is_none()
    );
}

#[test]
fn a_late_unreconciled_suffix_does_not_revoke_earlier_independent_receipts() {
    let mut s = assignment();
    s.source("gap reason=scope owner=0 frame=0 stage=0 snapshot=0 site=0");
    let p = prepared(&s.text());
    assert_eq!(p.observations().state, EvaluationState::Indeterminate);
    assert!(p.source_write(1).value.is_some());
    s.source("node snapshot=4 frame=0 stage=1 node=400200 parent=0 edge=0 tag_known=1 tag=50 shape=5 flags=0 type=0 data0=0 data1=7 data2=0 data3=0 complete=1 reason=ok");
    let p = prepared(&s.text());
    assert!(p.source_write(1).value.is_some());
    assert!(p.snapshot(4).value.is_none());
}

#[test]
fn independent_kind_stacks_cannot_hide_crossed_actual_call_lifetimes() {
    let mut s = Stream::new();
    s.source("mode.begin invocation=1 parent_mode=0 owner=0 input=0 mode_word=0 eval_mode=0 alloc_state=0 depth=1");
    s.source("clone.begin invocation=1 parent_clone=0 owner=0 mode_scope=1 input=0 caller=4f0000 eval_mode=0 alloc_state=0 depth=1");
    // Both individual stacks balance, but the actual calls cannot return in this order.
    s.source("mode.end invocation=1 parent_mode=0 owner=0 input=0 output=0 eval_mode=0 alloc_state=0 depth=0 input_snapshot=0 after_snapshot=0 output_snapshot=0");
    s.source("clone.end invocation=1 parent_clone=0 owner=0 mode_scope=1 input=0 output=0 eval_mode=0 alloc_state=0 depth=0 input_snapshot=0 after_snapshot=0 output_snapshot=0");
    s.clones = 1;
    s.checkpoint(0);
    let text = s
        .text()
        .replace("modes=0 mode_ends=0", "modes=1 mode_ends=1");
    let p = prepared(&text);
    assert!(p.clone_edge(1).value.is_none());
    assert!(
        p.observations()
            .reasons
            .iter()
            .any(|r| r.detail.contains("crossed source frame"))
    );
}

#[test]
fn stage_root_ids_cannot_hide_reordered_before_and_after_reads() {
    let mut s = Stream::new();
    s.clone_call(0);
    s.checkpoint(0);
    let text = s
        .text()
        .replace("stage=2", "stage=99")
        .replace("stage=3", "stage=2")
        .replace("stage=99", "stage=3")
        .replace(
            "input_snapshot=1 after_snapshot=2",
            "input_snapshot=2 after_snapshot=1",
        );
    assert!(prepared(&text).clone_edge(1).value.is_none());
}

#[test]
fn a_reused_node_address_does_not_preserve_allocation_or_observation_identity() {
    let mut s = Stream::new();
    let before = s.clone_call(0);
    s.arena("reset.begin reset=2 parent_reset=0 op=3 mask=8 lock_value=0 skip=0 request_depth=0 depth=1 epoch0=1 epoch1=1 epoch2=1 epoch3=2 epoch4=1");
    s.arena("reset.end reset=2 parent_reset=0 op=3 mask=8 skip=0 status_known=0 status=0 request_depth=0 depth=0 epoch0=1 epoch1=1 epoch2=1 epoch3=2 epoch4=1");
    s.allocations += 1;
    s.arena("request request=7 parent_request=0 pair=2 pool=3 requested=30 retail_esp=200000 site=440d25 epoch=2 depth=1");
    s.arena("alloc allocation=7 request=7 pair=2 pool=3 requested=30 requested_known=1 reserved=32 address=400280 block=300000 freep=4002a0 free=1024 retail_esp=200000 site=440d40 epoch=2 complete=1");
    let after = s.clone_call(0);
    s.checkpoint(0);
    let checkpoint = s
        .lines
        .iter_mut()
        .find(|line| line.starts_with("pv.arena.checkpoint"))
        .unwrap();
    *checkpoint = checkpoint
        .replace("resets=1 reset_ends=1", "resets=2 reset_ends=2")
        .replace("epoch3=1", "epoch3=2");
    let p = prepared(&s.text());
    let before = p.snapshot(before[0]).value.unwrap();
    let after = p.snapshot(after[0]).value.unwrap();
    assert_eq!(before.observation().node, after.observation().node);
    assert_eq!(before.observation().data, after.observation().data);
    assert_eq!(before.same_observation(&after).value, Some(false));
    assert_eq!(before.same_allocation(&after).value, Some(false));
    assert_ne!(before.allocation().epoch(), after.allocation().epoch());
}

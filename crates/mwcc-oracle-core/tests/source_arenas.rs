use mwcc_oracle_core::{
    evidence::EvaluationState,
    source_arenas::{self, History},
    source_lifetimes::PoolDomain,
};

struct Stream {
    lines: Vec<String>,
    requests: u32,
    allocations: u32,
    resets: u32,
    reset_ends: u32,
    gaps: u32,
    checkpoints: u32,
    epochs: [u32; 5],
}
impl Stream {
    fn new() -> Self {
        let mut s = Self {
            lines: vec![],
            requests: 0,
            allocations: 0,
            resets: 0,
            reset_ends: 0,
            gaps: 0,
            checkpoints: 0,
            epochs: [0; 5],
        };
        s.add(
            "pv.arena.begin scope=guarded_retail_arenas_v1 version=3 pools=100006 lock=100000"
                .into(),
        );
        s
    }
    fn add(&mut self, line: String) -> usize {
        let event = self
            .lines
            .iter()
            .filter(|l| l.starts_with("pv.arena."))
            .count()
            + 1;
        let (tag, fields) = line.split_once(' ').unwrap();
        self.lines
            .push(format!("{tag} schema=1 stream=1 event={event} {fields}"));
        self.lines.len()
    }
    fn epoch_fields(&self) -> String {
        self.epochs
            .iter()
            .enumerate()
            .map(|(i, e)| format!("epoch{i}={e}"))
            .collect::<Vec<_>>()
            .join(" ")
    }
    fn init(&mut self) {
        self.resets += 1;
        self.epochs.iter_mut().for_each(|e| *e += 1);
        self.add(format!("pv.arena.reset.begin reset={} parent_reset=0 op=7 mask=31 lock_value=0 skip=0 request_depth=0 depth=1 {}",self.resets,self.epoch_fields()));
        self.reset_ends += 1;
        self.add(format!("pv.arena.reset.end reset={} parent_reset=0 op=7 mask=31 skip=0 status_known=1 status=0 request_depth=0 depth=0 {}",self.resets,self.epoch_fields()));
    }
    fn local_reset(&mut self, locked: bool) -> (usize, usize) {
        self.resets += 1;
        if !locked {
            self.epochs[3] += 1;
        }
        let begin=self.add(format!("pv.arena.reset.begin reset={} parent_reset=0 op=3 mask=8 lock_value={} skip={} request_depth=0 depth=1 {}",self.resets,u8::from(locked),u8::from(locked),self.epoch_fields()));
        self.reset_ends += 1;
        let end=self.add(format!("pv.arena.reset.end reset={} parent_reset=0 op=3 mask=8 skip={} status_known=0 status=0 request_depth=0 depth=0 {}",self.resets,u8::from(locked),self.epoch_fields()));
        (begin, end)
    }
    fn local(&mut self, address: u32, requested: u32) -> usize {
        self.requests += 1;
        self.add(format!("pv.arena.request request={} parent_request=0 pair=2 pool=3 requested={requested} retail_esp=200000 site=440d25 epoch={} depth=1",self.requests,self.epochs[3]));
        self.allocations += 1;
        let reserved = (requested & !7) + 8;
        self.add(format!("pv.arena.alloc allocation={} request={} pair=2 pool=3 requested={requested} requested_known=1 reserved={reserved} address={address:x} block=300000 freep={:x} free=1024 retail_esp=200000 site=440d40 epoch={} complete=1",self.allocations,self.requests,address+reserved,self.epochs[3]))
    }
    fn checkpoint(&mut self) -> usize {
        self.checkpoints += 1;
        self.lines
            .push(format!("pv.function.begin function={}", self.checkpoints));
        let position=self.add(format!("pv.arena.checkpoint checkpoints={} requests={} allocations={} resets={} reset_ends={} gaps={} request_depth=0 reset_depth=0 unsupported={} function={} {}",self.checkpoints,self.requests,self.allocations,self.resets,self.reset_ends,self.gaps,u8::from(self.gaps!=0),self.checkpoints,self.epoch_fields()));
        self.lines
            .push(format!("pv.function.end function={}", self.checkpoints));
        position
    }
    fn gap(&mut self) -> usize {
        self.gaps += 1;
        self.add("pv.arena.gap reason=unsupported_callback mask=31 site=1234".into())
    }
    fn history(&self) -> History {
        source_arenas::parse(&self.lines.join("\n")).unwrap()
    }
}
fn positive(s: &Stream, position: usize, address: u32) -> u64 {
    let q = s
        .history()
        .resolve_at(position, PoolDomain::Local, address, 4);
    assert_eq!(q.observations.state, EvaluationState::Satisfied, "{q:?}");
    q.allocation.unwrap().allocation
}
fn unknown(s: &Stream, position: usize, address: u32) {
    let q = s
        .history()
        .resolve_at(position, PoolDomain::Local, address, 4);
    assert_eq!(
        q.observations.state,
        EvaluationState::Indeterminate,
        "{q:?}"
    );
    assert!(q.allocation.is_none());
}

#[test]
fn independent_checkpoint_proves_only_the_observed_historical_payload() {
    let mut s = Stream::new();
    s.init();
    let allocation = s.local(0x4000, 30);
    unknown(&s, allocation, 0x4000);
    let checkpoint = s.checkpoint();
    let history = s.history();
    assert_eq!(history.observations.state, EvaluationState::Satisfied);
    let q = history.resolve_at(allocation, PoolDomain::Local, 0x4006, 4);
    let receipt = q.allocation.unwrap();
    assert_eq!(
        (
            receipt.allocation,
            receipt.epoch,
            receipt.offset,
            receipt.extent,
            receipt.checkpoint_position
        ),
        (1, 1, 6, 4, checkpoint)
    );
    assert!(
        history
            .resolve_at(allocation, PoolDomain::Local, 0x401e, 1)
            .allocation
            .is_none()
    );
    assert!(
        history
            .resolve_at(checkpoint + 1, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_none()
    );
}

#[test]
fn reset_reuse_and_locked_reset_do_not_confuse_historical_and_current_lifetimes() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 8);
    s.checkpoint();
    let (inside, after) = s.local_reset(false);
    let second = s.local(0x4000, 8);
    s.checkpoint();
    assert_eq!(positive(&s, first, 0x4000), 1);
    assert_eq!(positive(&s, second, 0x4000), 2);
    unknown(&s, inside, 0x4000);
    unknown(&s, after, 0x4000);
    let (_, after_locked) = s.local_reset(true);
    s.checkpoint();
    assert_eq!(positive(&s, after_locked, 0x4000), 2);
}

#[test]
fn initial_epoch_without_observed_barrier_and_absent_v6_stream_are_unknown() {
    let absent = source_arenas::parse("pv.function.begin function=1\n").unwrap();
    assert!(
        absent
            .resolve_at(1, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_none()
    );
    let mut s = Stream::new();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    unknown(&s, a, 0x4000);
    s.init();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    assert_eq!(positive(&s, a, 0x4000), 2);
}

#[test]
fn every_dropped_reordered_or_mismatched_record_blocks_the_unreconciled_prefix() {
    let mut s = Stream::new();
    s.init();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    for remove in 0..s.lines.len() {
        let mut bad = Stream {
            lines: s.lines.clone(),
            ..Stream::new()
        };
        bad.lines.remove(remove);
        unknown(&bad, a, 0x4000);
    }
    for (from, to) in [
        ("request=1 pair=2", "request=2 pair=2"),
        (
            "requested=8 requested_known=1",
            "requested=9 requested_known=1",
        ),
        ("reserved=16", "reserved=8"),
        ("freep=4010", "freep=4018"),
        ("free=1024", "free=-1"),
        ("block=300000", "block=0"),
        ("requests=1 allocations=1", "requests=2 allocations=1"),
        ("epoch3=1", "epoch3=2"),
        ("status_known=1 status=0", "status_known=0 status=0"),
    ] {
        let mut bad = Stream {
            lines: s.lines.clone(),
            ..Stream::new()
        };
        bad.lines
            .iter_mut()
            .for_each(|line| *line = line.replace(from, to));
        unknown(&bad, a, 0x4000);
    }
    let mut bad = Stream {
        lines: s.lines.clone(),
        ..Stream::new()
    };
    bad.lines.swap(3, 4);
    unknown(&bad, a, 0x4000);
}

#[test]
fn explicitly_located_unsupported_callback_preserves_only_earlier_checkpointed_observations() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 8);
    let gap = s.gap();
    s.checkpoint();
    assert_eq!(positive(&s, first, 0x4000), 1);
    unknown(&s, gap, 0x4000);
    s.local_reset(false);
    s.checkpoint();
    unknown(&s, s.lines.len(), 0x4000);
    let mut bad = Stream::new();
    bad.init();
    let a = bad.local(0x4000, 8);
    bad.checkpoint();
    let checkpoint = bad
        .lines
        .iter_mut()
        .find(|l| l.starts_with("pv.arena.checkpoint"))
        .unwrap();
    checkpoint.push_str(" unexpected=1");
    unknown(&bad, a, 0x4000);
}

#[test]
fn later_truncated_suffix_does_not_erase_an_independently_reconciled_earlier_prefix() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 8);
    s.checkpoint();
    let second = s.local(0x5000, 8);
    s.lines.pop();
    assert_eq!(positive(&s, first, 0x4000), 1);
    unknown(&s, second, 0x5000);
    assert_eq!(
        s.history().observations.state,
        EvaluationState::Indeterminate
    );
}

#[test]
fn overlapping_reserved_spans_taint_all_candidates_until_a_fresh_reset() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 4);
    let second = s.local(0x4004, 4);
    s.checkpoint();
    assert_eq!(positive(&s, first, 0x4000), 1);
    unknown(&s, second, 0x4000);
    unknown(&s, second, 0x4004);
    s.local_reset(false);
    let third = s.local(0x4000, 4);
    s.checkpoint();
    assert_eq!(positive(&s, third, 0x4000), 3);
}

#[test]
fn serialized_display_coverage_is_not_a_deserializable_private_receipt() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 8);
    s.checkpoint();
    let mut display = serde_json::to_value(s.history()).unwrap();
    display["records"][4]["fields"]["requested"] = serde_json::json!("9");
    let forged: History = serde_json::from_value(display).unwrap();
    assert!(
        forged
            .resolve_at(first, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_none()
    );
    let mut display = serde_json::to_value(s.history()).unwrap();
    display["coverage_intervals"] = serde_json::json!([]);
    let display: History = serde_json::from_value(display).unwrap();
    assert!(
        display
            .resolve_at(first, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_some()
    );
    let mut display = serde_json::to_value(s.history()).unwrap();
    let end = display["records"]
        .as_array_mut()
        .unwrap()
        .last_mut()
        .unwrap();
    end["raw"] = serde_json::json!("pv.function.end function=999");
    let forged: History = serde_json::from_value(display).unwrap();
    assert!(
        forged
            .resolve_at(first, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_none()
    );
}

#[test]
fn original_trace_positions_are_preserved_across_unrelated_records() {
    let mut s = Stream::new();
    s.init();
    let first = s.local(0x4000, 8);
    s.checkpoint();
    let raw = s
        .lines
        .iter()
        .map(|l| {
            if l.starts_with("pv.arena.") {
                format!("unrelated x=1\n{l}")
            } else {
                l.clone()
            }
        })
        .collect::<Vec<_>>()
        .join("\n");
    let history = source_arenas::parse(&raw).unwrap();
    assert_eq!(history.events[0].trace_position, 2);
    assert!(
        history
            .resolve_at(first * 2, PoolDomain::Local, 0x4000, 4)
            .allocation
            .is_some()
    );
}

#[test]
fn checkpoint_origin_requires_an_independent_matching_backend_scope_or_reset_end() {
    let mut s = Stream::new();
    s.init();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    for change in 0..4 {
        let mut bad = Stream {
            lines: s.lines.clone(),
            ..Stream::new()
        };
        match change {
            0 => {
                let cp = bad
                    .lines
                    .iter_mut()
                    .find(|l| l.starts_with("pv.arena.checkpoint"))
                    .unwrap();
                *cp = cp.replace("function=1", "function=0");
            }
            1 => {
                let end = bad.lines.last_mut().unwrap();
                *end = end.replace("function=1", "function=2");
            }
            2 => {
                let at = bad.lines.len() - 1;
                bad.lines
                    .insert(at, "pv.value.birth function=1 sequence=99".into());
            }
            _ => {
                let begin = bad
                    .lines
                    .iter_mut()
                    .find(|l| l.starts_with("pv.function.begin"))
                    .unwrap();
                *begin = begin.replace("function=1", "function=2");
            }
        }
        unknown(&bad, a, 0x4000);
    }
    let mut reset = Stream::new();
    reset.init();
    reset.checkpoints = 1;
    reset.add(format!("pv.arena.checkpoint checkpoints=1 requests=0 allocations=0 resets=1 reset_ends=1 gaps=0 request_depth=0 reset_depth=0 unsupported=0 function=0 {}",reset.epoch_fields()));
    assert_eq!(
        reset.history().observations.state,
        EvaluationState::Satisfied
    );
}

#[test]
fn nested_requests_pair_exact_frames_and_preserve_actual_commit_order() {
    let mut s = Stream::new();
    s.init();
    s.requests = 2;
    s.allocations = 2;
    s.add("pv.arena.request request=1 parent_request=0 pair=2 pool=3 requested=8 retail_esp=200000 site=440d25 epoch=1 depth=1".into());
    s.add("pv.arena.request request=2 parent_request=1 pair=2 pool=3 requested=4 retail_esp=1fff00 site=440d25 epoch=1 depth=2".into());
    let first=s.add("pv.arena.alloc allocation=1 request=2 pair=2 pool=3 requested=4 requested_known=1 reserved=8 address=4000 block=300000 freep=4008 free=1024 retail_esp=1fff00 site=440d40 epoch=1 complete=1".into());
    let second=s.add("pv.arena.alloc allocation=2 request=1 pair=2 pool=3 requested=8 requested_known=1 reserved=16 address=4010 block=300000 freep=4020 free=1000 retail_esp=200000 site=440d40 epoch=1 complete=1".into());
    s.checkpoint();
    assert_eq!(positive(&s, first, 0x4000), 1);
    assert_eq!(positive(&s, second, 0x4010), 2);
    for (from, to) in [
        ("parent_request=1", "parent_request=0"),
        ("request=2 pair=2", "request=1 pair=2"),
        ("depth=2", "depth=1"),
    ] {
        let mut bad = Stream {
            lines: s.lines.clone(),
            ..Stream::new()
        };
        bad.lines
            .iter_mut()
            .for_each(|line| *line = line.replace(from, to));
        unknown(&bad, second, 0x4010);
    }
}

#[test]
fn nested_resets_require_exact_parent_order_epochs_and_return_status() {
    let mut s = Stream::new();
    s.init();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    s.resets = 3;
    s.reset_ends = 3;
    s.epochs = [3; 5];
    let begin=s.add("pv.arena.reset.begin reset=2 parent_reset=0 op=8 mask=31 lock_value=0 skip=0 request_depth=0 depth=1 epoch0=2 epoch1=2 epoch2=2 epoch3=2 epoch4=2".into());
    s.add("pv.arena.reset.begin reset=3 parent_reset=2 op=7 mask=31 lock_value=0 skip=0 request_depth=0 depth=2 epoch0=3 epoch1=3 epoch2=3 epoch3=3 epoch4=3".into());
    s.add("pv.arena.reset.end reset=3 parent_reset=2 op=7 mask=31 skip=0 status_known=1 status=0 request_depth=0 depth=1 epoch0=3 epoch1=3 epoch2=3 epoch3=3 epoch4=3".into());
    s.add("pv.arena.reset.end reset=2 parent_reset=0 op=8 mask=31 skip=0 status_known=1 status=0 request_depth=0 depth=0 epoch0=3 epoch1=3 epoch2=3 epoch3=3 epoch4=3".into());
    let b = s.local(0x4000, 8);
    s.checkpoint();
    assert_eq!(positive(&s, a, 0x4000), 1);
    unknown(&s, begin, 0x4000);
    assert_eq!(positive(&s, b, 0x4000), 2);
    let mut bad = Stream {
        lines: s.lines.clone(),
        ..Stream::new()
    };
    bad.lines
        .iter_mut()
        .for_each(|l| *l = l.replace("reset=3 parent_reset=2", "reset=3 parent_reset=0"));
    unknown(&bad, b, 0x4000);
}

#[test]
fn independent_unsupported_counts_and_pinned_sites_cannot_be_forged_as_complete() {
    let mut s = Stream::new();
    s.init();
    let a = s.local(0x4000, 8);
    s.checkpoint();
    for (from, to) in [
        ("unsupported=0", "unsupported=1"),
        ("gaps=0", "gaps=1"),
        ("site=440d25", "site=440d26"),
        ("site=440d40", "site=440d41"),
        ("stream=1", "stream=2"),
        ("schema=1", "schema=2"),
    ] {
        let mut bad = Stream {
            lines: s.lines.clone(),
            ..Stream::new()
        };
        bad.lines.iter_mut().for_each(|l| *l = l.replace(from, to));
        unknown(&bad, a, 0x4000);
    }
}

#[test]
fn checkpoint_emission_is_not_a_queryable_liveness_position_even_after_a_later_checkpoint() {
    let mut s = Stream::new();
    s.init();
    s.local(0x4000, 8);
    let first = s.checkpoint();
    unknown(&s, first, 0x4000);
    s.checkpoint();
    unknown(&s, first, 0x4000);
}

#[test]
fn arena_stack_capacity_matches_the_guarded_producers_sixty_four_frames() {
    let mut s = Stream::new();
    s.init();
    for id in 1..=65 {
        s.add(format!("pv.arena.request request={id} parent_request={} pair=2 pool=3 requested=8 retail_esp=200000 site=440d25 epoch=1 depth={id}",id-1));
    }
    let history = s.history();
    assert_eq!(history.events.len(), 3 + 64);
    assert_eq!(history.observations.state, EvaluationState::Indeterminate);
}

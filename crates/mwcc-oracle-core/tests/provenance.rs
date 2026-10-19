use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance::{self, Decision},
};

const BEGIN: &str = "pv.coalesce.begin schema=1 pass=1 cls=4 count=42 physical=32 first=32 last=42 special=1 creation=missing link=sample__Fv\n";
const CANDIDATE: &str = "pv.coalesce.candidate pass=1 candidate=1 instruction=00000100 flags=10 opcode=123 operands=2 destination_kind=0 destination_class=4 destination=35 source_kind=0 source_class=4 source=36\n";
fn check(condition: &str, site: &str, taken: u8, flags: &str) -> String {
    format!(
        "pv.coalesce.check pass=1 candidate=1 site={site} condition={condition} taken={taken} eflags={flags} eax=0 ebx=24 ecx=0 edx=23 esi=0 edi=0 ebp=0\n"
    )
}
fn prefix() -> String {
    format!(
        "{BEGIN}{CANDIDATE}{}{}{}{}{}",
        check("copy_flag", "10f", 0, "40"),
        check("destination_kind", "17b", 0, "40"),
        check("register_class", "18b", 0, "40"),
        check("roots_differ", "1d6", 1, "0"),
        check("interference", "244", 0, "40")
    )
}
#[test]
fn arena_coverage_is_independent_of_coalescer_decision_coverage() {
    let text = format!(
        "{}{}pv.coalesce.end pass=1 candidates=1\n",
        prefix(),
        check("destination_below_window", "271", 1, "80")
    );
    let with_arenas = format!("pv.arena.unknown schema=1\n{text}");
    let report = provenance::parse(&with_arenas).unwrap();
    assert_eq!(report.decisions.state, EvaluationState::Satisfied);
    assert_eq!(
        report.source_arenas.observations.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn branches_explain_rejections_but_never_prove_creation_or_elimination() {
    let trace = format!(
        "{}{}pv.coalesce.end pass=1 candidates=1\n",
        prefix(),
        check("destination_below_window", "271", 1, "80")
    );
    let report = provenance::parse(&trace).unwrap();
    assert_eq!(report.decisions.state, EvaluationState::Satisfied);
    assert_eq!(report.lifecycle.state, EvaluationState::Indeterminate);
    assert_eq!(
        report.passes[0].candidates[0].decision,
        Decision::Rejected {
            condition: "destination_below_window".into()
        }
    );
    let truncated =
        provenance::parse(&trace.replace("pv.coalesce.end pass=1 candidates=1\n", "")).unwrap();
    assert_eq!(truncated.decisions.state, EvaluationState::Indeterminate);
    assert_eq!(
        provenance::parse("").unwrap().decisions.state,
        EvaluationState::Indeterminate
    );
    let unsupported = provenance::parse(&trace.replace("schema=1", "schema=2")).unwrap();
    assert_eq!(unsupported.decisions.state, EvaluationState::Indeterminate);
    assert_eq!(
        unsupported.passes[0].decisions.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        unsupported.passes[0].candidates[0].evidence.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn merges_require_observed_write_and_the_whole_deciding_path() {
    let mut trace = prefix();
    for (condition, site) in [
        ("destination_below_window", "271"),
        ("destination_above_window", "280"),
        ("source_below_window", "289"),
        ("source_above_window", "291"),
    ] {
        trace += &check(condition, site, 0, "40");
    }
    trace += &check("gpr_special_register", "2b9", 0, "0");
    trace += "pv.coalesce.merge pass=1 candidate=1 parent=35 child=36 observed_parent=35\npv.coalesce.end pass=1 candidates=1\n";
    let report = provenance::parse(&trace).unwrap();
    assert_eq!(report.decisions.state, EvaluationState::Satisfied);
    assert_eq!(
        report.passes[0].candidates[0].decision,
        Decision::Merged {
            parent: 35,
            child: 36
        }
    );
    assert!(provenance::parse(&trace.replace("observed_parent=35", "observed_parent=36")).is_err());
    let missing = trace.replace(&check("source_below_window", "289", 0, "40"), "");
    assert_eq!(
        provenance::parse(&missing).unwrap().decisions.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn malformed_and_contradictory_branch_records_are_rejected() {
    let trace = format!(
        "{BEGIN}{CANDIDATE}{}pv.coalesce.end pass=1 candidates=1\n",
        check("copy_flag", "10f", 1, "0")
    );
    assert_eq!(
        provenance::parse(&trace).unwrap().decisions.state,
        EvaluationState::Satisfied
    );
    assert!(provenance::parse(&trace.replace("eflags=0", "eflags=40")).is_err());
    assert!(provenance::parse(&trace.replace("candidates=1", "candidates=2")).is_err());
    assert!(provenance::parse(&trace.replace("site=10f", "site=244")).is_err());
    assert!(provenance::parse(&format!("{trace}{CANDIDATE}")).is_err());
    let missing = provenance::parse(&format!(
        "{BEGIN}{CANDIDATE}pv.coalesce.end pass=1 candidates=1\n"
    ))
    .unwrap();
    assert_eq!(missing.decisions.state, EvaluationState::Indeterminate);
}

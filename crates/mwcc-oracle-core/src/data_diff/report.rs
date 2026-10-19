use super::{a, compare, linked, n, s};
use serde_json::Value;
fn short(s: &str) -> String {
    if s.chars().count() > 60 {
        format!("{}...", s.chars().take(57).collect::<String>())
    } else {
        s.into()
    }
}
pub fn line(mark: &str, i: &Value) -> String {
    let users = a(&i["users"]);
    let mut used = users.first().map_or(String::new(), |u| {
        format!(
            "  first used in {}{}",
            short(s(u)),
            if users.len() > 1 {
                format!(" (+{})", users.len() - 1)
            } else {
                String::new()
            }
        )
    });
    if let Some(made) = i.get("made") {
        let after = s(&made[0]);
        let before = s(&made[1]);
        used.push_str(&format!("; made earlier, before {}'s literals{}: by a function compiled there, not in the target or whose use of it was optimized away",short(before),if after.is_empty(){String::new()}else{format!(" (after {}'s)",short(after))}));
        if let Some(candidates) = i.get("candidates") {
            let candidates = a(candidates);
            if candidates.is_empty() {
                used.push_str(" (the oracle compiles no function there the target lacks)");
            } else {
                used.push_str(&format!(
                    " (the oracle compiles {} there: {}{})",
                    candidates.len(),
                    candidates
                        .iter()
                        .take(3)
                        .map(|c| short(s(c)))
                        .collect::<Vec<_>>()
                        .join(", "),
                    if candidates.len() > 3 { ", ..." } else { "" }
                ));
            }
        }
    }
    if let Some(via) = i.get("via") {
        used = format!(
            "  used through {} in {}{}",
            s(via),
            short(users.first().map_or("", s)),
            if users.len() > 1 {
                format!(" (+{})", users.len() - 1)
            } else {
                String::new()
            }
        );
    }
    let mut moved = i.get("moved").map_or(String::new(), |m| {
        format!("  [at {:#x} in the other]", n(m))
    });
    if let Some(e) = i.get("elsewhere") {
        moved.push_str(&format!("  [in {} in the other]", s(e)));
    }
    if mark == "+" && users.is_empty() && i["name"] != "(padding)" && i.get("dropped").is_none() {
        moved.push_str("  (nothing references it)");
    }
    let name = if !s(&i["name"]).is_empty() && i["name"] != "(padding)" {
        format!("  {}", s(&i["name"]))
    } else {
        String::new()
    };
    format!(
        "   {mark} {:#6x} {:#6x}  {}{name}{used}{moved}",
        n(&i["offset"]),
        n(&i["size"]),
        s(&i["value"])
    )
}
fn dropped(items: &[Value]) -> Vec<String> {
    let dropped: Vec<_> = items
        .iter()
        .filter(|i| i.get("dropped").is_some())
        .collect();
    if dropped.is_empty() {
        return vec![];
    }
    let unreferenced: Vec<_> = dropped
        .iter()
        .filter(|i| i["dropped"] == "unreferenced")
        .map(|i| s(&i["name"]))
        .collect();
    let users: Vec<_> = dropped
        .iter()
        .filter(|i| s(&i["dropped"]).starts_with("used "))
        .map(|i| format!("{} ({})", s(&i["name"]), &s(&i["dropped"])[5..]))
        .collect();
    let kept: Vec<_> = dropped
        .iter()
        .filter(|i| i["dropped"] != "unreferenced" && !s(&i["dropped"]).starts_with("used "))
        .map(|i| format!("{} ({})", s(&i["name"]), s(&i["dropped"])))
        .collect();
    let mut parts = vec![];
    if !unreferenced.is_empty() {
        parts.push(format!(
            "{} unreferenced ({}{})",
            unreferenced.len(),
            unreferenced
                .iter()
                .take(8)
                .copied()
                .collect::<Vec<_>>()
                .join(", "),
            if unreferenced.len() > 8 { ", ..." } else { "" }
        ));
    }
    if !kept.is_empty() {
        parts.push(format!(
            "{} weak, kept by another TU: {}{}",
            kept.len(),
            kept.iter().take(4).cloned().collect::<Vec<_>>().join(", "),
            if kept.len() > 4 { ", ..." } else { "" }
        ));
    }
    if !users.is_empty() {
        parts.push(format!(
            "{} {}{}",
            users.len(),
            users.iter().take(3).cloned().collect::<Vec<_>>().join("; "),
            if users.len() > 3 { "; ..." } else { "" }
        ));
    }
    vec![format!("     dropped when linked: {}", parts.join("; "))]
}
pub fn report(
    name: &str,
    mine: Option<&Value>,
    theirs: Option<&Value>,
    everything: bool,
) -> Vec<String> {
    if mine.is_none() || theirs.is_none() {
        let Some(section) = mine.or(theirs) else {
            return vec![];
        };
        let items = a(&section["items"]);
        if mine.is_some()
            && (linked(items).is_empty()
                || name.starts_with(".ctors")
                || name.starts_with(".dtors"))
            && !everything
        {
            return vec![];
        }
        let mut out = vec![format!(
            "== {name}: not in {} ({:#x} bytes, {} objects)",
            if mine.is_none() {
                "the oracle's object"
            } else {
                "the target"
            },
            n(&section["size"]),
            items.len()
        )];
        if mine.is_some() {
            out.extend(dropped(items));
        }
        let items = if mine.is_some() {
            linked(items)
        } else {
            items.to_vec()
        };
        out.extend(
            items
                .iter()
                .take(40)
                .map(|i| line(if theirs.is_none() { "+" } else { "-" }, i)),
        );
        return out;
    }
    let mine = mine.unwrap();
    let theirs = theirs.unwrap();
    let differences = compare(&linked(a(&mine["items"])), &linked(a(&theirs["items"])));
    let same = differences.iter().all(|d| d["tag"] == "equal");
    if same && !everything {
        return vec![];
    }
    let mut out = vec![format!(
        "== {name}: {}: oracle {:#x} bytes, align {}; target {:#x}, align {}  (- target, + oracle)",
        if same { "same once linked" } else { "differs" },
        n(&mine["size"]),
        n(&mine["align"]),
        n(&theirs["size"]),
        n(&theirs["align"])
    )];
    out.extend(dropped(a(&mine["items"])));
    let mut shown = 0;
    for d in differences {
        let target = a(&d["target"]);
        let oracle = a(&d["oracle"]);
        if d["tag"] == "equal" {
            out.push(format!(
                "     ({} the same, from {:#x} / {:#x}: {} ...)",
                target.len(),
                n(&target[0]["offset"]),
                n(&oracle[0]["offset"]),
                s(&target[0]["value"]).chars().take(40).collect::<String>()
            ));
            continue;
        }
        out.extend(target.iter().map(|i| line("-", i)));
        out.extend(oracle.iter().map(|i| line("+", i)));
        shown += target.len() + oracle.len();
        if shown > 40 {
            out.push("     ... (--json for all)".into());
            break;
        }
    }
    out
}

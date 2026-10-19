//! A plan: the candidates to try (a space), how each is evaluated (stages), in what order (a strategy), and how far
//! (a budget). Its candidates are rendered from the baseline's files before any is evaluated.
use crate::variants;
use anyhow::{Context, Result, bail};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, path::PathBuf};

/// The files of a candidate (a path relative to the project, and its text or `None`: absent).
pub type Files = BTreeMap<String, Option<String>>;

/// Exact replacements by file: each `[old, new]`, applied as the variants format applies them.
pub type Edits = BTreeMap<String, Vec<(String, String)>>;

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Plan {
    /// A new directory for the run (or one to resume).
    pub output: PathBuf,
    /// The files candidates may change (default: the unit's source).
    #[serde(default)]
    pub files: Vec<String>,
    /// The baseline's files in place of the disk's.
    #[serde(default)]
    pub baseline: Files,
    pub space: Space,
    #[serde(default = "default_stages")]
    pub stages: Vec<Stage>,
    #[serde(default)]
    pub strategy: Strategy,
    #[serde(default)]
    pub budget: Budget,
    /// The functions whose scores each row gives (decomp_experiment's `score_symbols`).
    #[serde(default)]
    pub symbols: Vec<String>,
    /// Candidates evaluated at once (default: the processors, at most 8).
    pub jobs: Option<usize>,
    /// Keep each candidate's object, and its diff (`diff.json`), in its directory.
    #[serde(default)]
    pub keep_objects: bool,
    #[serde(default)]
    pub diff: bool,
    /// Each row (and the control) with every target function's match (`functions`).
    #[serde(default)]
    pub functions: bool,
    /// A compile's limit.
    pub timeout_ms: Option<u64>,
}

fn default_stages() -> Vec<Stage> {
    vec![Stage::Compile, Stage::Score(ScoreStage::default())]
}

/// Where the candidates come from.
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "snake_case", deny_unknown_fields)]
pub enum Space {
    /// Explicit candidates.
    List(Vec<Candidate>),
    /// The variants format, applied to FILE (default: the unit's source): its text, or a file of it.
    Variants {
        file: Option<String>,
        text: Option<String>,
        path: Option<PathBuf>,
    },
    /// One choice from each axis (the Cartesian product); a choice's edits are merged with the others'.
    Axes(Vec<Vec<EditSet>>),
    /// Every combination of K of the edit sets (with `up_to`, of 1 to K).
    Combinations {
        k: usize,
        #[serde(default)]
        up_to: bool,
        sets: Vec<EditSet>,
    },
    /// The baseline with each `inline_max_size`.
    InlineMaxSize(Vec<u32>),
}

impl Default for Space {
    fn default() -> Self {
        Self::List(vec![])
    }
}

/// A named set of edits (an axis's choice, or one of a combination).
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct EditSet {
    pub name: String,
    #[serde(default)]
    pub edits: Edits,
}

/// A candidate as a plan gives it: complete files, edits of the baseline's, or both, and a flag.
#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Candidate {
    pub name: String,
    #[serde(default)]
    pub files: Files,
    #[serde(default)]
    pub edits: Edits,
    pub inline_max_size: Option<u32>,
    /// A variant `trace.realize` suggested: the model edit it realizes.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub suggested: Option<String>,
}

/// A stage of the evaluator, cheapest first; any can stop a candidate.
#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "snake_case", deny_unknown_fields)]
pub enum Stage {
    Compile,
    Score(ScoreStage),
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ScoreStage {
    /// Stop the candidate (`pruned`) when its unit's fuzzy match is below this, or the first symbol's.
    pub prune_if_fuzzy_below: Option<f64>,
    pub prune_if_symbol_below: Option<f64>,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum Strategy {
    /// Every candidate, in the space's order.
    #[default]
    Enumerate,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Budget {
    pub candidates: Option<usize>,
    pub compiles: Option<usize>,
    pub seconds: Option<f64>,
    /// The run stops when this file exists.
    pub cancel_file: Option<PathBuf>,
}

/// A candidate rendered: its complete files (each declared file's text, or absent), its flag, or why it cannot be.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Rendered {
    pub name: String,
    pub files: Files,
    pub inline_max_size: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub suggested: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

fn merge(into: &mut Edits, edits: &Edits) {
    for (file, list) in edits {
        into.entry(file.clone())
            .or_default()
            .extend(list.iter().cloned());
    }
}

/// The k-element combinations of 0..N, in lexicographic order.
fn combinations(n: usize, k: usize) -> Vec<Vec<usize>> {
    let mut out = vec![];
    let mut chosen: Vec<usize> = (0..k).collect();
    if k > n {
        return out;
    }
    loop {
        out.push(chosen.clone());
        let Some(i) = (0..k).rev().find(|&i| chosen[i] != i + n - k) else {
            return out;
        };
        chosen[i] += 1;
        for j in i + 1..k {
            chosen[j] = chosen[j - 1] + 1;
        }
    }
}

impl Space {
    /// The candidates of the space, as names, complete files, edits and flags (SOURCE: the unit's source).
    pub fn candidates(&self, source: &str) -> Result<Vec<Candidate>> {
        Ok(match self {
            Space::List(list) => list.clone(),
            Space::Variants { file, text, path } => {
                let text = match (text, path) {
                    (Some(text), None) => text.clone(),
                    (None, Some(path)) => std::fs::read_to_string(path)
                        .with_context(|| format!("read {}", path.display()))?,
                    _ => bail!("variants: give `text` or `path`"),
                };
                let file = file.clone().unwrap_or_else(|| source.to_owned());
                let (list, _) = variants::parse(&text)?;
                list.into_iter()
                    .map(|v| Candidate {
                        name: v.name,
                        edits: BTreeMap::from([(file.clone(), v.edits)]),
                        ..Candidate::default()
                    })
                    .collect()
            }
            Space::Axes(axes) => {
                let mut out = vec![Candidate::default()];
                for axis in axes {
                    let mut next = vec![];
                    for partial in &out {
                        for choice in axis {
                            let mut c = partial.clone();
                            c.name = if c.name.is_empty() {
                                choice.name.clone()
                            } else {
                                format!("{} + {}", c.name, choice.name)
                            };
                            merge(&mut c.edits, &choice.edits);
                            next.push(c);
                        }
                    }
                    out = next;
                }
                out
            }
            Space::Combinations { k, up_to, sets } => {
                let sizes: Vec<usize> = if *up_to { (1..=*k).collect() } else { vec![*k] };
                let mut out = vec![];
                for size in sizes {
                    for chosen in combinations(sets.len(), size) {
                        let mut c = Candidate {
                            name: chosen
                                .iter()
                                .map(|&i| sets[i].name.as_str())
                                .collect::<Vec<_>>()
                                .join(" + "),
                            ..Candidate::default()
                        };
                        for &i in &chosen {
                            merge(&mut c.edits, &sets[i].edits);
                        }
                        out.push(c);
                    }
                }
                out
            }
            Space::InlineMaxSize(sizes) => sizes
                .iter()
                .map(|&size| Candidate {
                    name: format!("inline_max_size({size})"),
                    inline_max_size: Some(size),
                    ..Candidate::default()
                })
                .collect(),
        })
    }
}

impl Candidate {
    /// The candidate's complete files over BASELINE (every declared file): its files, then its edits of them.
    pub fn render(&self, baseline: &Files) -> Rendered {
        let mut files = baseline.clone();
        let mut error = None;
        for (path, text) in &self.files {
            if !files.contains_key(path) {
                error.get_or_insert(format!("{path} is not one of the plan's files"));
            }
            files.insert(path.clone(), text.clone());
        }
        for (path, edits) in &self.edits {
            match files.get(path) {
                None => {
                    error.get_or_insert(format!("{path} is not one of the plan's files"));
                }
                Some(None) => {
                    error.get_or_insert(format!("{path} is absent: it has nothing to edit"));
                }
                Some(Some(text)) => match variants::apply(text, edits) {
                    Ok(edited) => {
                        files.insert(path.clone(), Some(edited));
                    }
                    Err(e) => {
                        error.get_or_insert(format!("{path}: {e}"));
                    }
                },
            }
        }
        Rendered {
            name: self.name.clone(),
            files,
            inline_max_size: self.inline_max_size,
            suggested: self.suggested.clone(),
            error,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn set(name: &str, old: &str, new: &str) -> EditSet {
        EditSet {
            name: name.into(),
            edits: BTreeMap::from([("a.c".to_owned(), vec![(old.to_owned(), new.to_owned())])]),
        }
    }

    #[test]
    fn spaces_give_their_candidates() {
        let axes = Space::Axes(vec![
            vec![set("a1", "1", "2"), set("a2", "1", "3")],
            vec![set("b1", "x", "y")],
        ]);
        let names: Vec<String> = axes
            .candidates("a.c")
            .unwrap()
            .into_iter()
            .map(|c| c.name)
            .collect();
        assert_eq!(names, ["a1 + b1", "a2 + b1"]);
        let sets = vec![set("p", "1", "2"), set("q", "x", "y"), set("r", "z", "w")];
        let pairs = Space::Combinations {
            k: 2,
            up_to: false,
            sets: sets.clone(),
        };
        let names: Vec<String> = pairs
            .candidates("a.c")
            .unwrap()
            .into_iter()
            .map(|c| c.name)
            .collect();
        assert_eq!(names, ["p + q", "p + r", "q + r"]);
        let all = Space::Combinations {
            k: 2,
            up_to: true,
            sets,
        };
        assert_eq!(all.candidates("a.c").unwrap().len(), 6);
        let variants = Space::Variants {
            file: None,
            text: Some("### v\n@@@ old\n1\n@@@ new\n2\n".into()),
            path: None,
        };
        let found = variants.candidates("a.c").unwrap();
        assert_eq!(found[0].edits["a.c"], [("1".to_owned(), "2".to_owned())]);
    }

    #[test]
    fn candidates_render_over_the_baseline() {
        let baseline: Files = BTreeMap::from([
            ("a.c".into(), Some("int x = 1;\n".into())),
            ("b.h".into(), None),
        ]);
        let c = Candidate {
            name: "c".into(),
            edits: set("c", "1", "2").edits,
            files: BTreeMap::from([("b.h".into(), Some("#define B\n".into()))]),
            ..Candidate::default()
        };
        let rendered = c.render(&baseline);
        assert_eq!(rendered.files["a.c"].as_deref(), Some("int x = 2;\n"));
        assert_eq!(rendered.files["b.h"].as_deref(), Some("#define B\n"));
        assert!(rendered.error.is_none());
        let bad = Candidate {
            name: "bad".into(),
            edits: set("bad", "9", "2").edits,
            ..Candidate::default()
        };
        assert!(bad.render(&baseline).error.unwrap().contains("0 times"));
        let stray = Candidate {
            name: "s".into(),
            files: BTreeMap::from([("c.c".into(), None)]),
            ..Candidate::default()
        };
        assert!(
            stray
                .render(&baseline)
                .error
                .unwrap()
                .contains("not one of the plan's files")
        );
    }
}

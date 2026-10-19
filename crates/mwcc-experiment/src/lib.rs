//! Experiments on a unit: a [`plan::Plan`] names a space of candidates, the stages that evaluate each, a strategy
//! and a budget; [`run::run`] runs it with its controls, writing `results.jsonl` as candidates finish.
pub mod plan;
pub mod run;
pub mod variants;

use serde::{Deserialize, Serialize};

/// Where time went, in milliseconds.
#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize, PartialEq)]
pub struct Timing {
    pub load: f64,
    pub compile: f64,
    pub score: f64,
    pub trace: f64,
    pub model: f64,
    pub queue: f64,
    pub other: f64,
}

impl Timing {
    pub fn add(&mut self, other: &Timing) {
        self.load += other.load;
        self.compile += other.compile;
        self.score += other.score;
        self.trace += other.trace;
        self.model += other.model;
        self.queue += other.queue;
        self.other += other.other;
    }

    pub fn total(&self) -> f64 {
        self.load + self.compile + self.score + self.trace + self.model + self.queue + self.other
    }

    /// The share of the time in compiling, scoring and models.
    pub fn productive(&self) -> f64 {
        let total = self.total();
        if total > 0.0 {
            (self.compile + self.score + self.model) / total
        } else {
            0.0
        }
    }
}

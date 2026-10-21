//! Experiments on a unit: a [`plan::Plan`] names a space of candidates, the stages that evaluate each, a strategy
//! and a budget; [`run::run`] runs it with its controls, writing `results.jsonl` as candidates finish.
pub mod plan;
pub mod run;
pub mod variants;

use serde::{Deserialize, Serialize};
use std::time::Duration;

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
}

/// Milliseconds of DURATION.
pub fn ms(duration: Duration) -> f64 {
    duration.as_secs_f64() * 1e3
}

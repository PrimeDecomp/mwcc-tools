//! Build progress reporting for a "dumb" console, without any overprinting.

use crate::progress::{build_message, Progress};
use crate::{
    graph::Build, graph::BuildId, process::Termination, task::TaskResult, work::StateCounts,
};
use std::cell::Cell;
use std::io::Write;

/// Progress implementation for "dumb" console, without any overprinting.
#[derive(Default)]
pub struct DumbConsoleProgress {
    /// Whether to print command lines of started programs.
    verbose: bool,

    /// The id of the last command printed, used to avoid printing it twice
    /// when we have two updates from the same command in a row.
    last_started: Cell<Option<BuildId>>,

    /// (mwcc-ninja: ninja's `[started/total]` before each command) The tasks started, and how many will have run
    /// once those wanted and not yet found up to date have.
    started: Cell<usize>,
    total: Cell<usize>,
}

impl DumbConsoleProgress {
    pub fn new(verbose: bool) -> Self {
        Self {
            verbose,
            last_started: Default::default(),
            started: Cell::new(0),
            total: Cell::new(0),
        }
    }
}

impl Progress for DumbConsoleProgress {
    fn update(&self, counts: &StateCounts) {
        use crate::work::BuildState::*;
        let running = counts.get(Running);
        let pending = counts.get(Want) + counts.get(Ready) + counts.get(Queued) + running;
        self.total
            .set(self.started.get() - running.min(self.started.get()) + pending);
    }

    fn task_started(&self, id: BuildId, build: &Build) {
        self.started.set(self.started.get() + 1);
        let total = self.total.get().max(self.started.get());
        self.log(&format!(
            "[{}/{}] {}",
            self.started.get(),
            total,
            if self.verbose {
                build.cmdline.as_ref().unwrap()
            } else {
                build_message(build)
            }
        ));
        self.last_started.set(Some(id));
    }

    fn task_output(&self, _id: BuildId, _line: Vec<u8>) {
        // ignore
    }

    fn task_finished(&self, id: BuildId, build: &Build, result: &TaskResult) {
        let mut hide_output = result.output.is_empty();
        match result.termination {
            Termination::Success => {
                if result.output.is_empty() || self.last_started.get() == Some(id) {
                    // Output is empty, or we just printed the command, don't print it again.
                } else {
                    self.log(build_message(build))
                }
                if build.hide_success {
                    hide_output = true;
                }
            }
            Termination::Interrupted => self.log(&format!("interrupted: {}", build_message(build))),
            Termination::Failure => self.log(&format!("failed: {}", build_message(build))),
        };
        if !hide_output {
            std::io::stdout().write_all(&result.output).unwrap();
        }
    }

    fn log(&self, msg: &str) {
        println!("{}", msg);
    }
}

//! A decomp project's units as its build compiles them, and their compiles.
//! - [`unit`]: [`Project`] (objdiff.json, build.ninja, report.json), [`Unit`], [`compile_unit`];
//! - [`ninja`]: the generated-Ninja subset decomp kits use;
//! - [`compilers`]: the module that runs a project's compiler in process, and the launcher of the others;
//! - [`paths`]: the project's paths (a unit's source by name, the target's directory and asm).
pub mod cache;
pub mod compilers;
pub mod ninja;
pub mod paths;
pub mod unit;

pub use cache::CompileCache;
pub use mwcc_wasm::{Overlay, PersistPool, PoolOptions, PoolStats};
pub use unit::{CompileOptions, Compiled, ModuleObjects, Persist, Project, Unit, compile_unit};

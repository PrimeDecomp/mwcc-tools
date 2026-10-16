//! Diagnostics use a separate namespace from generic temporary directories.
pub(crate) fn diagnostic_log() -> std::io::Result<tempfile::NamedTempFile> {
    // On Windows, create_new against an existing directory returns ACCESS_DENIED,
    // which tempfile cannot retry as an ordinary AlreadyExists collision. Old
    // .tmpXXXXXX directories in the shared temp root caused real corpus failures
    // (including case-only collisions). Keep logs out of that namespace and use
    // enough random characters for long-running parallel compiler workloads.
    tempfile::Builder::new()
        .prefix("mwcc-log-")
        .rand_bytes(16)
        .tempfile()
}

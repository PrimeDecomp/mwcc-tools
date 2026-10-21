//! Digests as the lowercase hexadecimal text the configurations and audits record.
use sha1::Sha1;
use sha2::{Digest, Sha256};

/// BYTES' SHA-256, in lowercase hexadecimal.
pub fn sha256_hex(bytes: impl AsRef<[u8]>) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

/// BYTES' SHA-1, in lowercase hexadecimal.
pub fn sha1_hex(bytes: impl AsRef<[u8]>) -> String {
    format!("{:x}", Sha1::digest(bytes))
}

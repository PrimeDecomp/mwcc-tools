//! Arena addresses taken from the Wii 1.0RC1 allocator and name-table routines.

pub const VERSION: u8 = 6;
pub const CALLBACK: u32 = 0x46f27a;

// The first instruction that rounds the request (or the size check for
// InitNameHash), followed by the subtraction committing the allocation.
pub const SITES: [(u32, u32); 9] = [
    (0x46ec35, 0x46ec43),
    (0x46ebf5, 0x46ec03),
    (0x46ebb5, 0x46ebc3),
    (0x46eb75, 0x46eb83),
    (0x46f6e0, 0x46f6fe),
    (0x46f820, 0x46f83b),
    (0x46f890, 0x46f8ab),
    (0x46f995, 0x46f9b0),
    (0x46fa25, 0x46fa40),
];

pub fn rounded(requested: u32) -> Option<u32> {
    Some(requested.checked_add(7)? & !7)
}

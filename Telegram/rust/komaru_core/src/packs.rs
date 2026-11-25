//! Who made a sticker or emoji pack, by the pack id.

/// The user id packed into the id, 0 when the id has another layout.
pub fn owner_id(id: u64) -> u64 {
    let high = id >> 32;
    match id as u32 {
        0x003F_0000..=0x003F_FFFF => high | (1 << 31),
        0..=0x00FF_FFFF => high,
        0xFF00_0000..=0xFF3F_FFFF => high | (1 << 31) | (1 << 32),
        0xFF40_0000..=0xFF7F_FFFF => high | (1 << 32),
        _ => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::owner_id;

    #[test]
    fn layouts() {
        assert_eq!(owner_id((1234 << 32) | 5), 1234);
        assert_eq!(owner_id((1234 << 32) | 0x003F_0001), 1234 | (1 << 31));
        assert_eq!(owner_id((1234 << 32) | 0xFF00_0001), 1234 | (1 << 31) | (1 << 32));
        assert_eq!(owner_id((1234 << 32) | 0xFF40_0001), 1234 | (1 << 32));
        assert_eq!(owner_id((1234 << 32) | 0x1000_0000), 0);
    }
}

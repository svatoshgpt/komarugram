//! Finds what in a JPEG or MP4 tells where, when and with what it was taken.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::sync::OnceLock;

use regex::bytes::Regex;

const MAX_MOOV_SIZE: u64 = 64 * 1024 * 1024;

/// Byte ranges of the file as offset and size.
pub type Ranges = Vec<(i64, i64)>;

struct Tiff<'a> {
    data: &'a [u8],
    start: usize,
    file_offset: i64,
    little: bool,
    out: &'a mut Ranges,
}

impl Tiff<'_> {
    fn has(&self, at: i64, size: i64) -> bool {
        at >= 0 && size >= 0 && self.start as i64 + at + size <= self.data.len() as i64
    }

    fn u16(&self, at: i64) -> u32 {
        if !self.has(at, 2) {
            return 0;
        }
        let i = self.start + at as usize;
        let (a, b) = (u32::from(self.data[i]), u32::from(self.data[i + 1]));
        if self.little { a | (b << 8) } else { (a << 8) | b }
    }

    fn u32(&self, at: i64) -> u32 {
        if !self.has(at, 4) {
            return 0;
        }
        let (a, b) = (self.u16(at), self.u16(at + 2));
        if self.little { a | (b << 16) } else { (a << 16) | b }
    }

    /// The entries of a directory as their position and tag.
    fn entries(&self, ifd: u32) -> Vec<(i64, u32)> {
        let ifd = i64::from(ifd);
        if ifd == 0 || !self.has(ifd, 2) {
            return Vec::new();
        }
        (0..i64::from(self.u16(ifd)))
            .map(|i| ifd + 2 + i * 12)
            .take_while(|&at| self.has(at, 12))
            .map(|at| (at, self.u16(at)))
            .collect()
    }

    fn zero(&mut self, at: i64, size: i64) {
        if size > 0 && self.has(at, size) {
            self.out.push((self.file_offset + self.start as i64 + at, size));
        }
    }

    fn zero_value(&mut self, entry: i64) {
        const SIZES: [i64; 13] = [0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8];
        let kind = self.u16(entry + 2) as usize;
        let size = SIZES.get(kind).copied().unwrap_or(0) * i64::from(self.u32(entry + 4));
        if size > 4 {
            self.zero(i64::from(self.u32(entry + 8)), size);
        } else {
            self.zero(entry + 8, size);
        }
    }

    fn scrub_exif(&mut self, ifd: u32) {
        for (entry, tag) in self.entries(ifd) {
            // Dates, owner, body serial, lens model and serial.
            if matches!(tag, 0x9003 | 0x9004 | 0xA430 | 0xA431 | 0xA434 | 0xA435) {
                self.zero_value(entry);
            }
        }
    }

    fn scrub_gps(&mut self, ifd: u32) {
        let at = i64::from(ifd);
        if ifd == 0 || !self.has(at, 2) {
            return;
        }
        for (entry, _) in self.entries(ifd) {
            self.zero_value(entry);
        }
        // An empty directory is left: no entries, no next one.
        self.zero(at, 2 + i64::from(self.u16(at)) * 12 + 4);
    }

    fn scrub(&mut self) {
        for (entry, tag) in self.entries(self.u32(4)) {
            match tag {
                // Make, model, software and date.
                0x010F | 0x0110 | 0x0131 | 0x0132 => self.zero_value(entry),
                0x8769 => self.scrub_exif(self.u32(entry + 8)),
                0x8825 => self.scrub_gps(self.u32(entry + 8)),
                _ => {}
            }
        }
    }
}

/// `data` is an APP1 payload that starts at `file_offset` in the file.
fn find_exif(data: &[u8], file_offset: i64, out: &mut Ranges) {
    const START: usize = 6;
    if !data.starts_with(b"Exif\0\0") || data.len() < START + 8 {
        return;
    }
    let little = match &data[START..START + 2] {
        b"II" => true,
        b"MM" => false,
        _ => return,
    };
    Tiff { data, start: START, file_offset, little, out }.scrub();
}

/// Reads up to `size` bytes at `offset`.
fn read_at(file: &mut (impl Read + Seek), offset: u64, size: u64) -> Vec<u8> {
    let mut result = Vec::new();
    if file.seek(SeekFrom::Start(offset)).is_ok() {
        let _ = file.take(size).read_to_end(&mut result);
    }
    result
}

fn find_jpeg(file: &mut (impl Read + Seek), out: &mut Ranges) {
    let mut offset = 2u64;
    loop {
        let header = read_at(file, offset, 4);
        if header.len() < 4 || header[0] != 0xFF {
            return;
        }
        let marker = header[1];
        let length = (u64::from(header[2]) << 8) | u64::from(header[3]);
        if marker == 0xDA || marker == 0xD9 || length < 2 {
            return;
        }
        if marker == 0xE1 {
            let data = read_at(file, offset + 4, length - 2);
            find_exif(&data, offset as i64 + 4, out);
        }
        offset += 2 + length;
    }
}

fn find_in_moov(body: &[u8], body_offset: i64, out: &mut Ranges) {
    // "©xyz" holds the location of Android and older iPhones.
    const XYZ: &[u8] = b"\xA9xyz";
    let mut from = 0;
    while let Some(found) = body[from..].windows(4).position(|w| w == XYZ) {
        let i = from + found;
        from = i + 4;
        if i < 4 {
            continue;
        }
        let size = u32::from_be_bytes([body[i - 4], body[i - 3], body[i - 2], body[i - 1]]);
        let size = size as usize;
        if size > 8 && i - 4 + size <= body.len() {
            out.push((body_offset + i as i64 + 4, size as i64 - 8));
        }
    }
    // Apple keeps it as an ISO 6709 string among its metadata keys.
    static ISO: OnceLock<Regex> = OnceLock::new();
    let iso = ISO.get_or_init(|| {
        Regex::new(r"(?-u)[+-]\d{1,2}\.\d+[+-]\d{1,3}\.\d+([+-]\d+(\.\d+)?)?(CRS[^/]*)?/")
            .unwrap()
    });
    for found in iso.find_iter(body) {
        out.push((body_offset + found.start() as i64, found.len() as i64));
    }
}

fn find_mp4(file: &mut (impl Read + Seek), total: u64, out: &mut Ranges) {
    let mut offset = 0u64;
    while offset + 8 <= total {
        let header = read_at(file, offset, 16);
        if header.len() < 8 {
            return;
        }
        let mut size = u64::from(u32::from_be_bytes([header[0], header[1], header[2], header[3]]));
        let mut skip = 8;
        if size == 1 && header.len() >= 16 {
            size = u64::from_be_bytes(header[8..16].try_into().unwrap());
            skip = 16;
        } else if size == 0 {
            size = total - offset;
        }
        if size < skip {
            return;
        }
        if &header[4..8] == b"moov" && size <= MAX_MOOV_SIZE {
            let body = read_at(file, offset + skip, size - skip);
            find_in_moov(&body, (offset + skip) as i64, out);
            return;
        }
        let Some(next) = offset.checked_add(size) else {
            return;
        };
        offset = next;
    }
}

fn find_in(file: &mut (impl Read + Seek), total: u64) -> Ranges {
    let mut result = Ranges::new();
    let head = read_at(file, 0, 12);
    if head.starts_with(&[0xFF, 0xD8]) {
        find_jpeg(file, &mut result);
    } else if head.len() >= 8 && &head[4..8] == b"ftyp" {
        find_mp4(file, total, &mut result);
    }
    result
}

/// JPEG EXIF (GPS, camera, dates, serials) and MP4 / MOV locations.
pub fn find(path: &str) -> Ranges {
    let Ok(mut file) = File::open(path) else {
        return Ranges::new();
    };
    let total = file.metadata().map_or(0, |m| m.len());
    find_in(&mut file, total)
}

#[cfg(test)]
mod tests {
    use super::find_in;
    use std::io::Cursor;

    fn ranges(bytes: &[u8]) -> Vec<(i64, i64)> {
        find_in(&mut Cursor::new(bytes), bytes.len() as u64)
    }

    fn entry(tag: u16, kind: u16, count: u32, value: u32) -> Vec<u8> {
        let mut result = Vec::new();
        result.extend(tag.to_le_bytes());
        result.extend(kind.to_le_bytes());
        result.extend(count.to_le_bytes());
        result.extend(value.to_le_bytes());
        result
    }

    #[test]
    fn jpeg_exif() {
        // IFD0 at 8 with a Make of 6 bytes at 38 and a GPS IFD at 44.
        let mut tiff = b"II\x2A\x00\x08\x00\x00\x00".to_vec();
        tiff.extend(2u16.to_le_bytes());
        tiff.extend(entry(0x010F, 2, 6, 38));
        tiff.extend(entry(0x8825, 4, 1, 44));
        tiff.extend(0u32.to_le_bytes());
        tiff.extend(b"Canon\0");
        tiff.extend(1u16.to_le_bytes());
        tiff.extend(entry(0x0001, 2, 2, 0x4E));
        tiff.extend(0u32.to_le_bytes());
        let mut jpeg = vec![0xFF, 0xD8, 0xFF, 0xE1];
        jpeg.extend(((tiff.len() + 8) as u16).to_be_bytes());
        jpeg.extend(b"Exif\0\0");
        let base = jpeg.len() as i64;
        jpeg.extend(&tiff);
        jpeg.extend([0xFF, 0xD9]);
        assert_eq!(
            ranges(&jpeg),
            vec![(base + 38, 6), (base + 44 + 2 + 8, 2), (base + 44, 18)],
        );
    }

    #[test]
    fn mp4_location() {
        let mut moov = Vec::new();
        let place = b"+55.7558+037.6173/";
        moov.extend(((place.len() + 8 + 4) as u32).to_be_bytes());
        moov.extend(b"\xA9xyz");
        moov.extend([0, 18, 0x15, 0xC7]);
        moov.extend(place);
        let mut file = b"\x00\x00\x00\x10ftypisom\x00\x00\x00\x00".to_vec();
        file.extend(((moov.len() + 8) as u32).to_be_bytes());
        file.extend(b"moov");
        let body = file.len() as i64;
        file.extend(&moov);
        assert_eq!(
            ranges(&file),
            vec![(body + 8, place.len() as i64 + 4), (body + 12, place.len() as i64)],
        );
    }

    #[test]
    fn other_files() {
        assert!(ranges(b"").is_empty());
        assert!(ranges(b"\x89PNG\r\n\x1a\n0000000000").is_empty());
        assert!(ranges(&[0xFF, 0xD8, 0xFF, 0xE1, 0xFF, 0xFF]).is_empty());
    }
}

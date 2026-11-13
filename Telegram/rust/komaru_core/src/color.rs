//! Finds "#rgb" and "#rrggbb" colors in UTF-16 text.

const HASH: u16 = b'#' as u16;

fn is_word(unit: u16) -> bool {
    u8::try_from(unit).is_ok_and(|c| c.is_ascii_alphanumeric() || c == b'_')
}

fn is_hex(unit: u16) -> bool {
    u8::try_from(unit).is_ok_and(|c| c.is_ascii_hexdigit())
}

/// How many digits the color starting with the "#" at `at` has.
fn digits_at(text: &[u16], at: usize) -> Option<usize> {
    if text[at] != HASH || (at > 0 && (is_word(text[at - 1]) || text[at - 1] == HASH)) {
        return None;
    }
    [6, 3].into_iter().find(|&count| {
        let end = at + 1 + count;
        end <= text.len()
            && text[at + 1..end].iter().all(|&unit| is_hex(unit))
            && text.get(end).map_or(true, |&unit| !is_word(unit))
    })
}

/// Where each color ends, with the count of its digits.
pub fn find(text: &[u16]) -> Vec<(usize, usize)> {
    let mut result = Vec::new();
    let mut i = 0;
    while i < text.len() {
        if let Some(count) = digits_at(text, i) {
            i += 1 + count;
            result.push((i, count));
        } else {
            i += 1;
        }
    }
    result
}

#[cfg(test)]
mod tests {
    use super::find;

    fn ends(text: &str) -> Vec<(usize, usize)> {
        find(&text.encode_utf16().collect::<Vec<_>>())
    }

    #[test]
    fn finds() {
        assert_eq!(ends("#fff"), vec![(4, 3)]);
        assert_eq!(ends("цвет #FFaa00, и #0af."), vec![(12, 6), (20, 3)]);
        assert_eq!(ends("(#123456)"), vec![(8, 6)]);
    }

    #[test]
    fn skips() {
        assert!(ends("#ffff").is_empty());
        assert!(ends("a#fff").is_empty());
        assert!(ends("##fff").is_empty());
        assert!(ends("#fffg").is_empty());
        assert!(ends("#ggg #12").is_empty());
        assert_eq!(ends("#fff#000"), vec![(4, 3)]);
    }
}

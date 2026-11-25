//! Message filters: regular expressions run over the text of every message.
//!
//! The `regex` crate matches in linear time, so no filter can hang the
//! client the way a backtracking engine can. Patterns it cannot express,
//! such as lookaround or backreferences, are left to ICU on the C++ side.

use regex::bytes::{Regex, RegexBuilder};

// Bounds the compiled program, so a huge repetition fails to compile
// instead of taking memory.
const SIZE_LIMIT: usize = 8 << 20;

pub struct Pattern(Regex);

impl Pattern {
    /// The pattern with ICU's multiline flag, or None when this engine
    /// cannot run it.
    pub fn compile(pattern: &str, case_insensitive: bool) -> Option<Pattern> {
        RegexBuilder::new(pattern)
            .multi_line(true)
            .case_insensitive(case_insensitive)
            .size_limit(SIZE_LIMIT)
            .build()
            .ok()
            .map(Pattern)
    }

    /// Whether the pattern occurs anywhere in UTF-8 `text`.
    pub fn find(&self, text: &[u8]) -> bool {
        self.0.is_match(text)
    }
}

#[cfg(test)]
mod tests {
    use super::Pattern;

    fn matches(pattern: &str, case_insensitive: bool, text: &str) -> bool {
        Pattern::compile(pattern, case_insensitive).unwrap().find(text.as_bytes())
    }

    #[test]
    fn matching() {
        assert!(matches("реклама", false, "Тут реклама канала"));
        assert!(!matches("реклама", false, "Тут РЕКЛАМА канала"));
        assert!(matches("реклама", true, "Тут РЕКЛАМА канала"));
        assert!(matches(r"^\d+$", false, "first line\n12345\nlast"));
        assert!(matches(r"\bt\.me/\w+", false, "join t.me/some_channel now"));
        assert!(!matches(r"\bcat\b", false, "concatenate"));
    }

    #[test]
    fn unsupported_patterns_go_to_icu() {
        assert!(Pattern::compile(r"foo(?=bar)", false).is_none());
        assert!(Pattern::compile(r"(a)\1", false).is_none());
        assert!(Pattern::compile(r"(unclosed", false).is_none());
    }

    #[test]
    fn no_catastrophic_backtracking() {
        let text = "a".repeat(5000) + "!";
        let started = std::time::Instant::now();
        assert!(!matches(r"^(a+)+$", false, &text));
        assert!(started.elapsed().as_secs() < 2);
    }
}

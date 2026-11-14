//! Completes "1 грам =" with the amount in a currency and "2+2=" with 4.

use std::sync::OnceLock;

use regex::Regex;

struct Coin {
    id: &'static str,
    names: &'static [&'static str],
}

struct Fiat {
    code: &'static str,
    sign: &'static str,
    names: &'static [&'static str],
}

const COINS: &[Coin] = &[
    Coin { id: "tether", names: &["usdt", "юсдт", "тезер"] },
    Coin {
        id: "the-open-network",
        names: &[
            "gram", "grams", "грам", "грамм", "грама", "грамов", "граммов", "ton", "тон", "тонов",
        ],
    },
    Coin { id: "litecoin", names: &["ltc", "лтц", "лайт", "лайтов"] },
    Coin { id: "bitcoin", names: &["btc", "бтц", "биток", "битка", "битков"] },
];

const FIATS: &[Fiat] = &[
    Fiat { code: "rub", sign: "₽", names: &["₽", "rub", "руб", "р"] },
    Fiat { code: "usd", sign: "$", names: &["$", "usd", "долл"] },
    Fiat { code: "eur", sign: "€", names: &["€", "eur", "евро"] },
];

/// The number with a decimal comma and narrow spaces between thousands.
fn format(value: f64) -> String {
    let decimals = if value >= 1000. { 0 } else if value >= 1. { 2 } else { 6 };
    group(format!("{value:.decimals$}"), decimals > 0)
}

/// Groups the thousands of a plain decimal number, `trim` drops trailing zeros.
fn group(mut number: String, trim: bool) -> String {
    if trim && number.contains('.') {
        number.truncate(number.trim_end_matches('0').trim_end_matches('.').len());
    }
    let (sign, number) = match number.strip_prefix('-') {
        Some(rest) if rest.bytes().any(|b| b.is_ascii_digit() && b != b'0') => ("\u{2212}", rest),
        Some(rest) => ("", rest),
        None => ("", number.as_str()),
    };
    let (whole, fraction) = match number.split_once('.') {
        Some((whole, fraction)) => (whole, Some(fraction)),
        None => (number, None),
    };
    let mut result = String::from(sign);
    for (i, digit) in whole.chars().enumerate() {
        if i > 0 && (whole.len() - i) % 3 == 0 {
            result.push('\u{202F}');
        }
        result.push(digit);
    }
    if let Some(fraction) = fraction {
        result.push(',');
        result.push_str(fraction);
    }
    result
}

/// A recursive descent over "+ - * / ^" and brackets.
struct Calc<'a> {
    rest: &'a [u8],
    operators: usize,
}

impl Calc<'_> {
    fn peek(&mut self) -> Option<u8> {
        while let [b' ' | b'\t', rest @ ..] = self.rest {
            self.rest = rest;
        }
        self.rest.first().copied()
    }

    fn eat(&mut self, byte: u8) -> bool {
        let found = self.peek() == Some(byte);
        if found {
            self.rest = &self.rest[1..];
        }
        found
    }

    fn sum(&mut self, depth: usize) -> Option<f64> {
        let mut result = self.product(depth)?;
        loop {
            if self.eat(b'+') {
                result += self.product(depth)?;
            } else if self.eat(b'-') {
                result -= self.product(depth)?;
            } else {
                return Some(result);
            }
            self.operators += 1;
        }
    }

    fn product(&mut self, depth: usize) -> Option<f64> {
        let mut result = self.power(depth)?;
        loop {
            if self.eat(b'*') {
                result *= self.power(depth)?;
            } else if self.eat(b'/') {
                result /= self.power(depth)?;
            } else {
                return Some(result);
            }
            self.operators += 1;
        }
    }

    fn power(&mut self, depth: usize) -> Option<f64> {
        if self.eat(b'-') {
            return Some(-self.power(depth)?);
        }
        let base = self.atom(depth)?;
        if !self.eat(b'^') {
            return Some(base);
        }
        self.operators += 1;
        Some(base.powf(self.power(depth)?))
    }

    fn atom(&mut self, depth: usize) -> Option<f64> {
        if self.eat(b'(') {
            if depth > 32 {
                return None;
            }
            let result = self.sum(depth + 1)?;
            return self.eat(b')').then_some(result);
        }
        self.peek()?;
        let size = self
            .rest
            .iter()
            .position(|b| !b.is_ascii_digit() && *b != b'.')
            .unwrap_or(self.rest.len());
        let (number, rest) = self.rest.split_at(size);
        self.rest = rest;
        std::str::from_utf8(number).ok()?.parse().ok()
    }
}

/// The value of "2+2=", nothing for a lone number or a broken expression.
fn calculate(text: &str) -> Option<String> {
    let text = text.trim().strip_suffix('=')?;
    let plain = text
        .replace(',', ".")
        .replace(['\u{00D7}', '\u{00B7}'], "*")
        .replace('\u{00F7}', "/")
        .replace('\u{2212}', "-");
    let mut calc = Calc { rest: plain.as_bytes(), operators: 0 };
    let value = calc.sum(0)?;
    if calc.peek().is_some() || calc.operators == 0 || !value.is_finite() || value.abs() >= 1e15 {
        return None;
    }
    Some(group(format!("{value:.6}"), true))
}

/// `rate` gives the price of a coin id in a currency code, 0 when unknown.
pub fn completion(text: &str, rate: impl FnOnce(&str, &str) -> f64) -> Option<String> {
    price(text, rate).or_else(|| calculate(text))
}

fn price(text: &str, rate: impl FnOnce(&str, &str) -> f64) -> Option<String> {
    static QUERY: OnceLock<Regex> = OnceLock::new();
    let query = QUERY.get_or_init(|| {
        Regex::new(r"^\s*(\d+(?:[.,]\d+)?)\s*([^\s\d=]+)\s*=\s*([^\s\d]*)\s*$").unwrap()
    });
    let found = query.captures(text)?;
    let amount: f64 = found[1].replace(',', ".").parse().ok()?;
    let name = found[2].to_lowercase();
    let target = found[3].to_lowercase();
    let coin = COINS.iter().find(|coin| coin.names.contains(&name.as_str()))?;
    let fiat = if target.is_empty() {
        &FIATS[0]
    } else {
        FIATS.iter().find(|fiat| fiat.names.contains(&target.as_str()))?
    };
    let rate = rate(coin.id, fiat.code);
    if rate.is_nan() || rate <= 0. {
        return None;
    }
    let mut result = format(amount * rate);
    if target.is_empty() {
        result.push(' ');
        result.push_str(fiat.sign);
    }
    Some(result)
}

#[cfg(test)]
mod tests {
    use super::{calculate, completion, format};

    fn rate(coin: &str, fiat: &str) -> f64 {
        match (coin, fiat) {
            ("the-open-network", "rub") => 81.85,
            ("bitcoin", "usd") => 100000.,
            ("tether", "eur") => 0.000125,
            _ => 0.,
        }
    }

    #[test]
    fn completes() {
        assert_eq!(completion("1 грам =", rate).as_deref(), Some("81,85 ₽"));
        assert_eq!(completion(" 3,0 TON = ", rate).as_deref(), Some("245,55 ₽"));
        assert_eq!(completion("1.5 btc = $", rate).as_deref(), Some("150\u{202F}000"));
        assert_eq!(completion("1 usdt = евро", rate).as_deref(), Some("0,000125"));
    }

    #[test]
    fn calculates() {
        assert_eq!(completion("1/1=", rate).as_deref(), Some("1"));
        assert_eq!(completion("1btc=$", rate).as_deref(), Some("100\u{202F}000"));
        assert_eq!(calculate("2 + 2 * 2 =").as_deref(), Some("6"));
        assert_eq!(calculate("(2+2)*2=").as_deref(), Some("8"));
        assert_eq!(calculate("1,5 × 4 − 10=").as_deref(), Some("\u{2212}4"));
        assert_eq!(calculate("2^3^2=").as_deref(), Some("512"));
        assert_eq!(calculate("-2^2=").as_deref(), Some("\u{2212}4"));
        assert_eq!(calculate("10/3=").as_deref(), Some("3,333333"));
        assert_eq!(calculate("1000*1000=").as_deref(), Some("1\u{202F}000\u{202F}000"));
        assert_eq!(calculate("0-0.0000001=").as_deref(), Some("0"));
        assert_eq!(calculate("5="), None);
        assert_eq!(calculate("-5="), None);
        assert_eq!(calculate("1/0="), None);
        assert_eq!(calculate("2+="), None);
        assert_eq!(calculate("(2+2="), None);
        assert_eq!(calculate("2+2"), None);
        assert_eq!(calculate("2+x="), None);
    }

    #[test]
    fn rejects() {
        assert_eq!(completion("1 грам", rate), None);
        assert_eq!(completion("1 doge =", rate), None);
        assert_eq!(completion("1 грам = тугрик", rate), None);
        assert_eq!(completion("1 ltc =", rate), None);
        assert_eq!(completion("грам =", rate), None);
    }

    #[test]
    fn formats() {
        assert_eq!(format(1234567.8), "1\u{202F}234\u{202F}568");
        assert_eq!(format(12.5), "12,5");
        assert_eq!(format(3.), "3");
        assert_eq!(format(0.5), "0,5");
    }
}

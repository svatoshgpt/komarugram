//! Completes "1 грам =" with the amount in a currency.

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
    let mut number = format!("{value:.decimals$}");
    if decimals > 0 {
        number.truncate(number.trim_end_matches('0').trim_end_matches('.').len());
    }
    let (whole, fraction) = match number.split_once('.') {
        Some((whole, fraction)) => (whole, Some(fraction)),
        None => (number.as_str(), None),
    };
    let mut result = String::new();
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

/// `rate` gives the price of a coin id in a currency code, 0 when unknown.
pub fn completion(text: &str, rate: impl FnOnce(&str, &str) -> f64) -> Option<String> {
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
    use super::{completion, format};

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

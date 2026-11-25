//! A tiny `--name value` parser: the commands take only a few options.

use std::collections::HashMap;

pub struct Args {
    values: HashMap<String, Vec<String>>,
    pub positional: Vec<String>,
}

impl Args {
    pub fn parse(list: &[String]) -> Result<Args, String> {
        let mut result = Args { values: HashMap::new(), positional: vec![] };
        let mut i = 0;
        while i < list.len() {
            let item = &list[i];
            if let Some(name) = item.strip_prefix("--") {
                if let Some((name, value)) = name.split_once('=') {
                    result.values.entry(name.into()).or_default().push(value.into());
                } else {
                    let value = list.get(i + 1).ok_or(format!("--{name} needs a value"))?;
                    result.values.entry(name.into()).or_default().push(value.clone());
                    i += 1;
                }
            } else {
                result.positional.push(item.clone());
            }
            i += 1;
        }
        Ok(result)
    }

    pub fn get(&self, name: &str) -> Option<&str> {
        self.values.get(name).and_then(|v| v.last()).map(String::as_str)
    }

    /// Every value of an option that may repeat.
    pub fn all(&self, name: &str) -> &[String] {
        self.values.get(name).map_or(&[], Vec::as_slice)
    }

    pub fn required(&self, name: &str) -> Result<&str, String> {
        self.get(name).ok_or(format!("--{name} is required"))
    }

    pub fn number(&self, name: &str) -> Result<Option<i64>, String> {
        self.get(name)
            .map(|v| v.parse().map_err(|_| format!("--{name} must be a number")))
            .transpose()
    }

}

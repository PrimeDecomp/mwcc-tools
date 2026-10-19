use super::*;
use std::{collections::BTreeMap, thread, time::Duration};
pub trait Fetch {
    fn fetch(&mut self, url: &str, range: Option<(u64, u64)>) -> Result<Vec<u8>>;
}
pub struct HttpFetcher {
    agent: ureq::Agent,
    servers: BTreeMap<String, Vec<String>>,
    pub attempts: u32,
    pub backoff: Duration,
}
impl HttpFetcher {
    pub fn new(timeout: Duration) -> Result<Self> {
        ensure!(!timeout.is_zero(), "download timeout must be positive");
        Ok(Self {
            agent: ureq::Agent::config_builder()
                .timeout_global(Some(timeout))
                .build()
                .into(),
            servers: BTreeMap::new(),
            attempts: 6,
            backoff: Duration::from_secs(1),
        })
    }
    fn get(&self, url: &str, range: Option<(u64, u64)>) -> Result<Vec<u8>> {
        let mut request = self.agent.get(url);
        if let Some((start, end)) = range {
            request = request.header("Range", format!("bytes={start}-{end}"));
        }
        let mut response = request.call()?;
        if let Some((start, end)) = range {
            ensure!(
                response.status().as_u16() == 206,
                "{url}: server did not honor the byte range"
            );
            let header = response
                .headers()
                .get("Content-Range")
                .context("range response lacks Content-Range")?
                .to_str()?;
            let fields = header
                .strip_prefix("bytes ")
                .context("invalid Content-Range unit")?
                .split_once('/')
                .context("invalid Content-Range")?;
            let (a, b) = fields
                .0
                .split_once('-')
                .context("invalid Content-Range bounds")?;
            let a: u64 = a.parse()?;
            let b: u64 = b.parse()?;
            ensure!(
                a == start && b >= a && b <= end,
                "range response does not match the requested bytes"
            );
            let length = b
                .checked_sub(a)
                .and_then(|n| n.checked_add(1))
                .context("HTTP byte range length overflow")?;
            let total = if fields.1 == "*" {
                None
            } else {
                Some(fields.1.parse::<u64>()?)
            };
            ensure!(
                total.is_none_or(|total| b < total),
                "range exceeds resource length"
            );
            ensure!(
                b == end || total == b.checked_add(1),
                "short range does not end at EOF"
            );
            ensure!(length <= MAX_BYTES, "range response is too large");
            let data = response
                .body_mut()
                .with_config()
                // ureq's limit reader needs one extra byte to observe EOF.
                .limit(length + 1)
                .read_to_vec()?;
            ensure!(data.len() as u64 == length, "truncated HTTP byte range");
            return Ok(data);
        }
        let data = response
            .body_mut()
            .with_config()
            .limit(MAX_BYTES + 1)
            .read_to_vec()?;
        ensure!(data.len() as u64 <= MAX_BYTES, "HTTP response is too large");
        Ok(data)
    }
    fn retried(&self, url: &str, range: Option<(u64, u64)>) -> Result<Vec<u8>> {
        ensure!(self.attempts > 0, "download attempts must be positive");
        for attempt in 0..self.attempts {
            match self.get(url, range) {
                Ok(data) => return Ok(data),
                Err(error) => {
                    let client_error = matches!(error.downcast_ref::<ureq::Error>(),Some(ureq::Error::StatusCode(code)) if *code<500);
                    if attempt + 1 == self.attempts || client_error {
                        return Err(error).with_context(|| format!("download {url}"));
                    }
                    eprintln!("{url}: {error}; retrying");
                    thread::sleep(
                        self.backoff
                            .saturating_mul(1u32.checked_shl(attempt).unwrap_or(u32::MAX)),
                    );
                }
            }
        }
        unreachable!()
    }
}
impl Fetch for HttpFetcher {
    fn fetch(&mut self, url: &str, range: Option<(u64, u64)>) -> Result<Vec<u8>> {
        let Some(path) = url.strip_prefix("https://archive.org/download/") else {
            return self.retried(url, range);
        };
        let (item, file) = path
            .split_once('/')
            .context("invalid Internet Archive download URL")?;
        if file.contains(".7z/") {
            return self.retried(url, range);
        }
        if !self.servers.contains_key(item) {
            let metadata: serde_json::Value = serde_json::from_slice(
                &self.retried(&format!("https://archive.org/metadata/{item}"), None)?,
            )?;
            let directory = metadata["dir"]
                .as_str()
                .context("archive metadata lacks directory")?;
            let servers = metadata["workable_servers"]
                .as_array()
                .context("archive metadata lacks servers")?
                .iter()
                .map(|server| {
                    Ok(format!(
                        "https://{}{directory}/",
                        server.as_str().context("archive server is not text")?
                    ))
                })
                .collect::<Result<Vec<_>>>()?;
            ensure!(
                !servers.is_empty(),
                "archive metadata lists no workable servers"
            );
            self.servers.insert(item.into(), servers);
        }
        let servers = self.servers[item].clone();
        for (i, server) in servers.iter().enumerate() {
            match self.retried(&format!("{server}{file}"), range) {
                Ok(data) => return Ok(data),
                Err(error) if i + 1 == servers.len() => return Err(error),
                Err(error) => eprintln!("{server}: {error}; trying the next server"),
            }
        }
        unreachable!()
    }
}

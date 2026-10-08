//! Self-update from the project's GitHub repository.
//!
//! The manifest at `<base>build/linux/latest.json` names the newest version
//! and, per CPU architecture, a file path relative to `<base>`, its SHA-256
//! and its size. `<base>` is PULSE_UPDATE_URL when set, else the raw
//! GitHub URL of the main branch. Pulse only goes on the network when the
//! user picks Check for Updates or passes --update-check or --update.
//!
//! HTTP goes through GIO sockets (TLS from glib-networking), so no HTTP
//! crate is needed and gvfs does not have to be running.

use gtk::gio;
use gtk::glib;
use gtk::prelude::*;
use std::path::{Path, PathBuf};

pub const CURRENT: &str = env!("CARGO_PKG_VERSION");
const DEFAULT_BASE: &str = "https://raw.githubusercontent.com/njarrar/SystemPulse/main/";
const MANIFEST: &str = "build/linux/latest.json";
const MAX_BYTES: usize = 256 << 20;
const MAX_REDIRECTS: usize = 5;

pub fn base_url() -> String {
    let mut b = std::env::var("PULSE_UPDATE_URL").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| DEFAULT_BASE.into());
    if !b.ends_with('/') {
        b.push('/');
    }
    b
}

#[derive(Clone, Debug, PartialEq)]
pub struct Release {
    pub version: String,
    pub url: String,
    pub sha256: String,
    pub size: Option<u64>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Check {
    /// Version in the manifest.
    pub latest: String,
    /// The build for this architecture, when the manifest has one.
    pub release: Option<Release>,
}

impl Check {
    /// The release to offer: newer than this build and built for this CPU.
    pub fn offer(&self) -> Option<&Release> {
        self.release.as_ref().filter(|_| newer(&self.latest, CURRENT))
    }
}

fn parts(v: &str) -> Vec<u64> {
    let v = v.trim().trim_start_matches(['v', 'V']);
    let mut p: Vec<u64> = v
        .split(['.', '-', '+'])
        .take_while(|s| s.starts_with(|c: char| c.is_ascii_digit()))
        .map(|s| s.chars().take_while(char::is_ascii_digit).collect::<String>().parse().unwrap_or(0))
        .collect();
    while p.last() == Some(&0) {
        p.pop();
    }
    p
}

/// True when dotted version `a` is newer than `b` ("1.10" > "1.9",
/// "1.2" == "1.2.0").
pub fn newer(a: &str, b: &str) -> bool {
    parts(a) > parts(b)
}

pub fn parse_manifest(text: &[u8], base: &str, arch: &str) -> Result<Check, String> {
    let doc: serde_json::Value = serde_json::from_slice(text).map_err(|e| format!("latest.json: {e}"))?;
    let latest = doc["version"].as_str().filter(|v| !parts(v).is_empty()).ok_or("latest.json has no version")?.to_string();
    let release = match doc["files"].get(arch) {
        None => None,
        Some(f) => {
            let path = f["path"].as_str().ok_or_else(|| format!("latest.json: no path for {arch}"))?;
            let sha256 = f["sha256"].as_str().filter(|s| s.len() == 64).ok_or_else(|| format!("latest.json: no sha256 for {arch}"))?;
            let url = if path.contains("://") { path.to_string() } else { format!("{base}{}", path.trim_start_matches('/')) };
            Some(Release { version: latest.clone(), url, sha256: sha256.to_ascii_lowercase(), size: f["size"].as_u64() })
        }
    };
    Ok(Check { latest, release })
}

/// Fetches the manifest.
pub async fn check() -> Result<Check, String> {
    let base = base_url();
    let text = get(&format!("{base}{MANIFEST}")).await?;
    parse_manifest(&text, &base, std::env::consts::ARCH)
}

/// Downloads the release and checks its size and SHA-256.
pub async fn download(r: &Release) -> Result<Vec<u8>, String> {
    let data = get(&r.url).await?;
    if let Some(n) = r.size {
        if data.len() as u64 != n {
            return Err(format!("download is {} bytes, expected {n}", data.len()));
        }
    }
    let mut sum = glib::Checksum::new(glib::ChecksumType::Sha256).ok_or("no SHA-256")?;
    sum.update(&data);
    let got = sum.string().unwrap_or_default();
    if got != r.sha256 {
        return Err(format!("checksum mismatch (got {got}, expected {})", r.sha256));
    }
    Ok(data)
}

/// The running binary. After a replace the kernel reports the old inode
/// as "<path> (deleted)"; the path itself is what we want.
fn exe_path() -> Result<PathBuf, String> {
    let exe = std::env::current_exe().map_err(|e| format!("cannot find the running program: {e}"))?;
    let s = exe.to_string_lossy();
    Ok(match s.strip_suffix(" (deleted)") {
        Some(p) => PathBuf::from(p),
        None => exe,
    })
}

/// Writes `<exe>.new` next to the running binary, makes it executable and
/// renames it over the binary (atomic on one filesystem).
pub fn install(data: &[u8]) -> Result<PathBuf, String> {
    use std::io::Write;
    use std::os::unix::fs::PermissionsExt;
    let exe = exe_path()?;
    let dir = exe.parent().unwrap_or(Path::new("/")).to_path_buf();
    let name = exe.file_name().ok_or("no program name")?.to_string_lossy().into_owned();
    let tmp = dir.join(format!("{name}.new"));
    let r = (|| -> std::io::Result<()> {
        let mut f = std::fs::File::create(&tmp)?;
        f.write_all(data)?;
        f.sync_all()?;
        std::fs::set_permissions(&tmp, std::fs::Permissions::from_mode(0o755))?;
        std::fs::rename(&tmp, &exe)
    })();
    r.map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        match e.kind() {
            std::io::ErrorKind::PermissionDenied | std::io::ErrorKind::ReadOnlyFilesystem => {
                format!("{} is not writable ({e}); move Pulse to a folder you can write to", dir.display())
            }
            _ => format!("cannot replace {}: {e}", exe.display()),
        }
    })?;
    Ok(exe)
}

/// Starts the new binary with the same arguments.
pub fn relaunch(exe: &Path) -> Result<(), String> {
    std::process::Command::new(exe)
        .args(std::env::args_os().skip(1))
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("cannot start {}: {e}", exe.display()))
}

/// `--update-check` (install false) and `--update` (install true).
pub fn cli(install_it: bool) -> i32 {
    glib::MainContext::default().block_on(async move {
        let c = match check().await {
            Ok(c) => c,
            Err(e) => {
                eprintln!("pulse: update check failed: {e}");
                return 1;
            }
        };
        println!("current={CURRENT} latest={}", c.latest);
        if c.release.is_none() {
            eprintln!("pulse: latest.json has no build for {}", std::env::consts::ARCH);
        }
        if !install_it {
            return 0;
        }
        let Some(r) = c.offer() else {
            if c.release.is_none() && newer(&c.latest, CURRENT) {
                println!("nothing to install");
            } else {
                println!("Pulse {CURRENT} is up to date");
            }
            return 0;
        };
        println!("downloading {}", r.url);
        match download(r).await.and_then(|d| install(&d)) {
            Ok(exe) => {
                println!("updated {} to {}", exe.display(), r.version);
                0
            }
            Err(e) => {
                eprintln!("pulse: update failed: {e}");
                1
            }
        }
    })
}

/// GET over http, https or file, following redirects.
pub async fn get(url: &str) -> Result<Vec<u8>, String> {
    let mut url = url.to_string();
    for _ in 0..=MAX_REDIRECTS {
        let uri = glib::Uri::parse(&url, glib::UriFlags::NONE).map_err(|e| format!("{url}: {e}"))?;
        let scheme = uri.scheme().to_ascii_lowercase();
        if scheme == "file" {
            let (bytes, _) = gio::File::for_uri(&url).load_contents_future().await.map_err(|e| format!("{url}: {e}"))?;
            return Ok(bytes.to_vec());
        }
        let tls = match scheme.as_str() {
            "https" => true,
            "http" => false,
            s => return Err(format!("{url}: unsupported scheme {s}")),
        };
        let (status, location, body) = http_get(&uri, &url, tls).await.map_err(|e| format!("{url}: {e}"))?;
        match status {
            200 => return Ok(body),
            301 | 302 | 303 | 307 | 308 => {
                let loc = location.ok_or_else(|| format!("{url}: redirect without Location"))?;
                url = uri.parse_relative(&loc, glib::UriFlags::NONE).map_err(|e| format!("{loc}: {e}"))?.to_str().to_string();
            }
            s => return Err(format!("{url}: HTTP {s}")),
        }
    }
    Err(format!("{url}: too many redirects"))
}

async fn http_get(uri: &glib::Uri, url: &str, tls: bool) -> Result<(u32, Option<String>, Vec<u8>), String> {
    let client = gio::SocketClient::new();
    client.set_tls(tls);
    client.set_timeout(30);
    let port = if tls { 443 } else { 80 };
    let conn = client.connect_to_uri_future(url, port).await.map_err(|e| e.to_string())?;
    let host = uri.host().map(|h| h.to_string()).unwrap_or_default();
    let host = if host.contains(':') { format!("[{host}]") } else { host };
    let host = if uri.port() > 0 && uri.port() != port as i32 { format!("{host}:{}", uri.port()) } else { host };
    let mut target = uri.path().to_string();
    if target.is_empty() {
        target.push('/');
    }
    if let Some(q) = uri.query() {
        target.push('?');
        target.push_str(&q);
    }
    let req = format!(
        "GET {target} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: Pulse/{CURRENT}\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"
    );
    conn.output_stream().write_all_future(req.into_bytes(), glib::Priority::DEFAULT).await.map_err(|(_, e)| e.to_string())?;
    let input = conn.input_stream();
    let mut raw = Vec::new();
    loop {
        // Some TLS servers close without close_notify; a complete response
        // is enough, so stop as soon as we have one.
        match parse_response(&raw, false) {
            Ok(Some(r)) => return Ok(r),
            Err(e) => return Err(e),
            Ok(None) => {}
        }
        match input.read_bytes_future(64 * 1024, glib::Priority::DEFAULT).await {
            Ok(b) if b.is_empty() => break,
            Ok(b) => raw.extend_from_slice(&b),
            Err(e) if raw.is_empty() => return Err(e.to_string()),
            Err(_) => break,
        }
        if raw.len() > MAX_BYTES {
            return Err("response too large".into());
        }
    }
    let _ = conn.close_future(glib::Priority::DEFAULT).await;
    parse_response(&raw, true)?.ok_or_else(|| "connection closed before the response was complete".into())
}

/// Parses an HTTP/1.x response. Ok(None) while more data is needed; at
/// end of stream a body without a length runs to the end.
fn parse_response(raw: &[u8], eof: bool) -> Result<Option<(u32, Option<String>, Vec<u8>)>, String> {
    let Some(end) = raw.windows(4).position(|w| w == b"\r\n\r\n") else {
        return if eof { Err("bad HTTP response".into()) } else { Ok(None) };
    };
    let head = String::from_utf8_lossy(&raw[..end]);
    let mut lines = head.split("\r\n");
    let status_line = lines.next().unwrap_or("");
    let status: u32 = status_line.split_whitespace().nth(1).and_then(|s| s.parse().ok()).ok_or_else(|| format!("bad status line: {status_line}"))?;
    let (mut len, mut chunked, mut location) = (None, false, None);
    for l in lines {
        let Some((k, v)) = l.split_once(':') else { continue };
        let v = v.trim();
        match k.trim().to_ascii_lowercase().as_str() {
            "content-length" => len = v.parse::<usize>().ok(),
            "transfer-encoding" => chunked = v.to_ascii_lowercase().contains("chunked"),
            "location" => location = Some(v.to_string()),
            _ => {}
        }
    }
    let body = &raw[end + 4..];
    if chunked {
        return match dechunk(body) {
            Some(b) => Ok(Some((status, location, b))),
            None if eof => Err("truncated chunked response".into()),
            None => Ok(None),
        };
    }
    match len {
        Some(n) if body.len() >= n => Ok(Some((status, location, body[..n].to_vec()))),
        Some(_) if eof => Err("truncated response".into()),
        None if eof => Ok(Some((status, location, body.to_vec()))),
        _ => Ok(None),
    }
}

/// Decodes a chunked body; None while it is incomplete.
fn dechunk(mut b: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    loop {
        let nl = b.windows(2).position(|w| w == b"\r\n")?;
        let line = std::str::from_utf8(&b[..nl]).ok()?;
        let n = usize::from_str_radix(line.split(';').next()?.trim(), 16).ok()?;
        b = &b[nl + 2..];
        if n == 0 {
            return Some(out);
        }
        if b.len() < n + 2 {
            return None;
        }
        out.extend_from_slice(&b[..n]);
        b = &b[n + 2..];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_order() {
        assert!(newer("1.3.0", "1.2.2"));
        assert!(newer("1.10", "1.9.9"));
        assert!(newer("2", "1.99"));
        assert!(newer("9.9.9", CURRENT));
        assert!(!newer("1.2.0", "1.2"));
        assert!(!newer("1.2", "1.2.0"));
        assert!(!newer("1.2.1", "1.2.2"));
        assert!(!newer("v1.2.2", "1.2.2"));
        assert!(newer("1.2.3-beta", "1.2.2"));
        assert!(!newer("", "0.1"));
    }

    #[test]
    fn manifest() {
        let j = br#"{"version":"1.3.0","files":{"x86_64":{"path":"build/linux/pulse","sha256":"AB000000000000000000000000000000000000000000000000000000000000CD","size":123}}}"#;
        let c = parse_manifest(j, "http://h/", "x86_64").unwrap();
        assert_eq!(c.latest, "1.3.0");
        let r = c.release.unwrap();
        assert_eq!(r.url, "http://h/build/linux/pulse");
        assert_eq!(r.size, Some(123));
        assert!(r.sha256.starts_with("ab"));
        let c = parse_manifest(j, "http://h/", "aarch64").unwrap();
        assert!(c.release.is_none() && c.offer().is_none());
        assert!(parse_manifest(b"{}", "http://h/", "x86_64").is_err());
    }

    #[test]
    fn http_parsing() {
        let r = b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
        assert_eq!(parse_response(r, false).unwrap().unwrap().2, b"hello");
        assert!(parse_response(&r[..r.len() - 1], false).unwrap().is_none());
        let c = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nhel\r\n2;x=y\r\nlo\r\n0\r\n\r\n";
        assert_eq!(parse_response(c, false).unwrap().unwrap().2, b"hello");
        let m = b"HTTP/1.1 302 Found\r\nLocation: /x\r\nContent-Length: 0\r\n\r\n";
        let (s, loc, _) = parse_response(m, false).unwrap().unwrap();
        assert_eq!((s, loc.as_deref()), (302, Some("/x")));
        let e = b"HTTP/1.0 200 OK\r\n\r\nall";
        assert!(parse_response(e, false).unwrap().is_none());
        assert_eq!(parse_response(e, true).unwrap().unwrap().2, b"all");
    }
}

use mwcc_oracle_core::download::{self, Disc, Fetch, HttpFetcher};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    fs,
    io::{Cursor, Read, Seek, SeekFrom, Write},
    net::TcpListener,
    path::{Path, PathBuf},
    thread,
    time::{Duration, Instant},
};

fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/download")
}
fn manifest() -> Value {
    serde_json::from_slice(&fs::read(fixtures().join("manifest.json")).unwrap()).unwrap()
}
fn sha(data: &[u8]) -> String {
    format!("{:x}", Sha256::digest(data))
}
struct MemoryFetch {
    data: Vec<u8>,
    requests: Vec<(u64, u64)>,
}
impl Fetch for MemoryFetch {
    fn fetch(&mut self, _: &str, range: Option<(u64, u64)>) -> anyhow::Result<Vec<u8>> {
        let (first, last) = range.unwrap();
        self.requests.push((first, last));
        Ok(self.data[first as usize..(last as usize + 1).min(self.data.len())].to_vec())
    }
}
#[test]
fn ranged_iso_zip_extraction_and_cache_match_the_reference() {
    let metadata = manifest();
    let logical = fs::read(fixtures().join("disc.iso")).unwrap();
    for case in metadata["discs"].as_array().unwrap() {
        let raw = case["raw"].as_bool().unwrap();
        let data = if raw {
            logical
                .chunks_exact(2048)
                .flat_map(|sector| [vec![0xa5; 24], sector.to_vec(), vec![0x5a; 280]].concat())
                .collect()
        } else {
            logical.clone()
        };
        let mut fetch = MemoryFetch {
            data,
            requests: vec![],
        };
        let mut disc = Disc::new(&mut fetch, "fixture", raw);
        for _ in 0..2 {
            let file = disc.file("CODEWA~1/CODEWA~1.ZIP;1").unwrap();
            let payload =
                download::zip_member(file, metadata["disc_member"].as_str().unwrap()).unwrap();
            assert_eq!(sha(&payload), case["member_sha256"]);
        }
        let mut file = disc.file("CODEWA~1/CODEWA~1.ZIP;1").unwrap();
        assert!(file.seek(SeekFrom::Start(1_000_000)).is_ok());
        assert_eq!(file.read(&mut [0; 10]).unwrap(), 0);
        assert!(file.seek(SeekFrom::End(-1_000_000)).is_err());
        assert!(disc.file("missing").is_err());
        assert_eq!(
            serde_json::to_value(&fetch.requests).unwrap(),
            case["requests"]
        );
    }
    let mut fetch = MemoryFetch {
        data: vec![0; 128 * 2048 - 1],
        requests: vec![],
    };
    assert!(
        Disc::new(&mut fetch, "fixture", false)
            .read(16, 2048)
            .is_err()
    );
    let mut fetch = MemoryFetch {
        data: vec![0; 128 * 2048],
        requests: vec![],
    };
    assert!(
        Disc::new(&mut fetch, "fixture", false)
            .file("anything")
            .is_err()
    );
}
fn tree(root: &Path, current: &Path, hashes: &mut BTreeMap<String, String>) {
    for entry in fs::read_dir(current).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            tree(root, &path, hashes);
        } else {
            hashes.insert(
                path.strip_prefix(root)
                    .unwrap()
                    .to_string_lossy()
                    .replace('\\', "/"),
                sha(&fs::read(&path).unwrap()),
            );
        }
    }
}
#[test]
fn library_selection_and_guarded_byte_patches_match_all_reference_outputs() {
    let root = tempfile::tempdir().unwrap();
    download::extract_library(
        Cursor::new(fs::read(fixtures().join("library.zip")).unwrap()),
        root.path(),
    )
    .unwrap();
    download::patch_library(root.path()).unwrap();
    let mut actual = BTreeMap::new();
    tree(root.path(), root.path(), &mut actual);
    let mut expected = manifest()["library_files"].as_object().unwrap().clone();
    expected.remove("ok");
    expected.remove("runtime53/nested/runtime.c");
    assert_eq!(
        serde_json::to_value(actual).unwrap(),
        Value::Object(expected)
    );
    let patched = fs::read(root.path().join("extra/printf.c")).unwrap();
    assert!(download::patch_library(root.path()).is_err());
    assert_eq!(
        fs::read(root.path().join("extra/printf.c")).unwrap(),
        patched
    );
}
#[test]
fn pins_platform_urls_and_fixture_integrity_match_the_reference() {
    // Rust calls Apple Silicon "aarch64"; the pinned release uses "arm64".
    assert_eq!(
        download::objdiff_url("Darwin", "aarch64").unwrap(),
        "https://github.com/encounter/objdiff/releases/download/v3.8.1/objdiff-cli-macos-arm64"
    );
    let metadata = manifest();
    assert_eq!(
        metadata["reference_commit"],
        "b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722"
    );
    for (file, hash) in metadata["files"].as_object().unwrap() {
        assert_eq!(
            sha(&fs::read(fixtures().join(file)).unwrap()),
            hash.as_str().unwrap()
        );
    }
    for case in metadata["urls"].as_array().unwrap() {
        let system = case["system"].as_str().unwrap();
        let machine = case["machine"].as_str().unwrap();
        assert_eq!(
            download::objdiff_url(system, machine).unwrap(),
            case["objdiff"]
        );
        assert_eq!(download::wibo_url(system, machine), case["wibo"]);
    }
}
fn zipped(name: &str) -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(Cursor::new(vec![]));
    zip.start_file(name, zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(b"fixture").unwrap();
    zip.finish().unwrap().into_inner()
}
#[test]
fn updater_crc_lengths_and_archive_paths_are_checked() {
    let metadata = manifest();
    let data = fs::read(fixtures().join("updater.bin")).unwrap();
    let offsets: Vec<_> = metadata["updater_offsets"]
        .as_array()
        .unwrap()
        .iter()
        .map(|n| n.as_u64().unwrap() as usize)
        .collect();
    let members: BTreeMap<_, _> = download::updater_entries(&data, &offsets)
        .unwrap()
        .into_iter()
        .map(|(n, d)| (n, sha(&d)))
        .collect();
    assert_eq!(
        serde_json::to_value(members).unwrap(),
        metadata["updater_members"]
    );
    for length in [0, 20, data.len() / 2] {
        assert!(download::updater_entries(&data[..length], &offsets).is_err());
    }
    let mut corrupt = data.clone();
    corrupt[offsets[0] + 14] ^= 1;
    assert!(download::updater_entries(&corrupt, &offsets).is_err());
    let root = tempfile::tempdir().unwrap();
    for name in [
        "../escape",
        "C:/escape",
        "/escape",
        "nested/../../escape",
        "..\\escape",
    ] {
        assert!(
            download::extract_zip(Cursor::new(zipped(name)), root.path()).is_err(),
            "{name}"
        );
    }
    assert!(fs::read_dir(root.path()).unwrap().next().is_none());
}
struct NeverFetch;
impl Fetch for NeverFetch {
    fn fetch(&mut self, url: &str, _: Option<(u64, u64)>) -> anyhow::Result<Vec<u8>> {
        panic!("unexpected network access: {url}")
    }
}
#[test]
fn fixed_artifact_cache_keeps_bytes_and_refreshes_the_windows_license_stub() {
    let root = tempfile::tempdir().unwrap();
    fs::create_dir_all(root.path().join("tools/lmgr_stub")).unwrap();
    fs::write(root.path().join("tools/lmgr_stub/lmgr326b.dll"), b"stub").unwrap();
    fs::create_dir_all(root.path().join("out")).unwrap();
    for name in ["compilers", "pro4", "pro5", "pro53", "pro6", "mwld", "lib"] {
        fs::write(root.path().join("out/tool"), b"cached tool").unwrap();
        download::run_with(root.path(), name, Path::new("out/tool"), &mut NeverFetch).unwrap();
        assert_eq!(
            fs::read(root.path().join("out/tool")).unwrap(),
            b"cached tool"
        );
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        let dll = root.path().join("out/lmgr326b.dll");
        assert_eq!(fs::read(&dll).unwrap(), b"stub");
        // Simulate the read sharing of an already loaded compiler license DLL.
        let held = fs::OpenOptions::new()
            .read(true)
            .share_mode(1)
            .open(&dll)
            .unwrap();
        download::run_with(root.path(), "mwld", Path::new("out/tool"), &mut NeverFetch).unwrap();
        drop(held);
    }
    assert!(
        download::run_with(
            root.path(),
            "unknown",
            Path::new("out/tool"),
            &mut NeverFetch
        )
        .is_err()
    );
}
fn server(responses: Vec<String>) -> (String, thread::JoinHandle<Vec<String>>) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let address = format!("http://{}/tool", listener.local_addr().unwrap());
    listener.set_nonblocking(true).unwrap();
    let handle = thread::spawn(move || {
        let mut requests = vec![];
        for response in responses {
            let began = Instant::now();
            let mut socket = loop {
                match listener.accept() {
                    Ok((socket, _)) => break socket,
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                        assert!(
                            began.elapsed() < Duration::from_secs(5),
                            "request did not arrive"
                        );
                        thread::sleep(Duration::from_millis(5));
                    }
                    Err(e) => panic!("{e}"),
                }
            };
            // Accepted sockets can inherit the listener's nonblocking mode on
            // Windows. The request loop below uses blocking reads with a timeout.
            socket.set_nonblocking(false).unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            let mut request = vec![];
            while !request.ends_with(b"\r\n\r\n") {
                let mut byte = [0];
                assert_eq!(socket.read(&mut byte).unwrap(), 1);
                request.push(byte[0]);
                assert!(request.len() < 8192);
            }
            requests.push(String::from_utf8(request).unwrap());
            socket.write_all(response.as_bytes()).unwrap();
        }
        requests
    });
    (address, handle)
}
#[test]
fn real_http_checks_ranges_retries_servers_and_does_not_retry_client_errors() {
    let mut fetch = HttpFetcher::new(Duration::from_secs(2)).unwrap();
    fetch.attempts = 2;
    fetch.backoff = Duration::ZERO;
    let (url, server) = server(vec![
        "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".into(),
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 10-13/14\r\nContent-Length: 4\r\nConnection: close\r\n\r\ndata".into(),
    ]);
    assert_eq!(fetch.fetch(&url, Some((10, 20))).unwrap(), b"data");
    let requests = server.join().unwrap();
    assert_eq!(requests.len(), 2);
    assert!(
        requests
            .iter()
            .all(|r| r.to_lowercase().contains("range: bytes=10-20\r\n"))
    );
    let (url, server) = self::server(vec![
        "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".into(),
    ]);
    let error = fetch.fetch(&url, None).unwrap_err();
    assert!(format!("{error:#}").contains("404"));
    assert_eq!(server.join().unwrap().len(), 1);
    fetch.attempts = 1;
    for (status, range, content) in [
        ("200 OK", "", "data"),
        (
            "206 Partial Content",
            "Content-Range: bytes 11-14/20\r\n",
            "data",
        ),
        (
            "206 Partial Content",
            "Content-Range: bytes 10-13/20\r\n",
            "data",
        ),
        (
            "206 Partial Content",
            "Content-Range: bytes 10-13/13\r\n",
            "data",
        ),
        (
            "206 Partial Content",
            "Content-Range: bytes 10-13/14\r\n",
            "abc",
        ),
    ] {
        let (url, server) = self::server(vec![format!(
            "HTTP/1.1 {status}\r\n{range}Content-Length: {}\r\nConnection: close\r\n\r\n{content}",
            content.len()
        )]);
        assert!(fetch.fetch(&url, Some((10, 20))).is_err());
        server.join().unwrap();
    }
    assert!(HttpFetcher::new(Duration::ZERO).is_err());
}

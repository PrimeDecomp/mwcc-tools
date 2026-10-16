"""Match actual native producer images/inputs to independent Python/native links."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_link import image_bytes
from verify_producer import sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--link-results", type=Path, required=True)
    args = parser.parse_args()
    stage = args.directory.resolve()
    manifest_path = stage / "producer-verification.json"
    manifest = json.loads(manifest_path.read_text())
    links = json.loads(args.link_results.read_text())
    assert manifest["complete"]
    assert len(links) == len(manifest["versions"])
    assert {v["version"] for v in links} == {v["version"] for v in manifest["versions"]}
    evidence = dict(native_manifest_sha256=sha(manifest_path), link_manifest_sha256=sha(args.link_results), versions=[])
    for version in manifest["versions"]:
        name = version["version"]
        linked = next(v for v in links if v["version"] == name)
        assert linked["identical"] is True and linked["timestamp_normalized"] is True
        assert Path(linked["compiled_oracle_root"]).resolve() == stage
        assert linked["compiled_oracle_inputs"]
        for path, digest in linked["compiled_oracle_inputs"].items():
            assert sha(stage / path) == digest, path
        hook = stage / f"build/{name}/oracle/compiled/rust/hooks.obj"
        assert sha(hook) == version["rust_object_sha256"] == linked["rust_hook_sha256"]
        if "frontend_object_sha256" in version:
            assert sha(stage / f"build/{name}/oracle/compiled/rust/frontend.obj") == version["frontend_object_sha256"] == linked["frontend_object_sha256"]
        executable = stage / f"build/{name}/oracle/mwcceppc.exe"
        assert sha(executable) == version["producer_sha256"]
        normalized = hashlib.sha256(image_bytes(executable)).hexdigest()
        assert normalized == linked["sha256"]
        evidence["versions"].append(dict(version=name, producer_sha256=sha(executable), normalized_image_sha256=normalized,
            hooks_sha256=sha(hook), direct_abi_cases=linked["direct_abi_cases"], compiled_inputs=len(linked["compiled_oracle_inputs"])))
    (stage / "native-link-audit.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"{stage}: every actual native image and oracle input matches independent linker evidence")


if __name__ == "__main__":
    main()

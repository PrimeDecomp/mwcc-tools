"""Host mapping and executable-format checks; no native macOS execution implied."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


INSTALL = load("kit_install", "integrations/decomp-kits/install-native.py")
DISCOVERY = load("kit_discovery", "integrations/decomp-kits/overlay/scripts/_mwcc_native.py")
AUDIT = load("release_audit", "tools/migration/audit_release.py")


class NativePlatforms(unittest.TestCase):
    def test_exact_host_targets_and_aliases(self):
        cases = [
            ("Windows", "AMD64", "x86_64-pc-windows-msvc"),
            ("Linux", "x86_64", "x86_64-unknown-linux-gnu"),
            ("Darwin", "x86_64", "x86_64-apple-darwin"),
            ("Darwin", "arm64", "aarch64-apple-darwin"),
            ("Darwin", "aarch64", "aarch64-apple-darwin"),
        ]
        for system, machine, expected in cases:
            with self.subTest(system=system, machine=machine):
                for module in (INSTALL, DISCOVERY, AUDIT):
                    with patch.object(module.platform, "system", return_value=system), patch.object(module.platform, "machine", return_value=machine):
                        self.assertEqual(module.native_target(), expected)

    def test_unsupported_hosts_never_select_linux(self):
        for system, machine in [("Linux", "aarch64"), ("Windows", "arm64"), ("FreeBSD", "x86_64"), ("Darwin", "ppc")]:
            for module in (INSTALL, DISCOVERY, AUDIT):
                with self.subTest(system=system, machine=machine, module=module.__name__), patch.object(module.platform, "system", return_value=system), patch.object(module.platform, "machine", return_value=machine):
                    with self.assertRaises(RuntimeError):
                        module.native_target()

    def test_missing_mac_artifact_has_readable_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            kit = Path(temporary)
            (kit / "native-manifest.json").write_text(json.dumps({"format_version": 1, "artifacts": {}}))
            error = io.StringIO()
            with patch.object(INSTALL, "__file__", str(kit / "install-native.py")), patch.object(INSTALL.platform, "system", return_value="Darwin"), patch.object(INSTALL.platform, "machine", return_value="arm64"), patch("sys.argv", ["install-native.py", temporary]), contextlib.redirect_stderr(error):
                with self.assertRaises(SystemExit) as raised:
                    INSTALL.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn("no bundled tools for aarch64-apple-darwin", error.getvalue())
            self.assertNotIn("Traceback", error.getvalue())

    def test_mac_installed_bundle_discovery(self):
        for machine, target in [("arm64", "aarch64-apple-darwin"), ("x86_64", "x86_64-apple-darwin")]:
            with self.subTest(machine=machine), tempfile.TemporaryDirectory() as temporary:
                project = Path(temporary)
                binary = project / ".agents/tools/mwcc" / target / "bin/mwcc-tools"
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b"test fixture")
                with patch.object(DISCOVERY, "__file__", str(project / "scripts/_mwcc_native.py")), patch.object(DISCOVERY.platform, "system", return_value="Darwin"), patch.object(DISCOVERY.platform, "machine", return_value=machine), patch.dict(DISCOVERY.os.environ, {}, clear=True), patch.object(DISCOVERY.shutil, "which", return_value=None):
                    self.assertEqual(DISCOVERY.executable([]), str(binary.resolve()))

    def test_explicit_and_path_fallback_on_unsupported_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "custom-tool"
            binary.write_bytes(b"test fixture")
            with patch.object(DISCOVERY.platform, "system", return_value="FreeBSD"), patch.dict(DISCOVERY.os.environ, {"MWCC_TOOLS": str(binary)}, clear=True):
                self.assertEqual(DISCOVERY.executable([]), str(binary))
            with patch.object(DISCOVERY.platform, "system", return_value="FreeBSD"), patch.dict(DISCOVERY.os.environ, {}, clear=True), patch.object(DISCOVERY.shutil, "which", return_value=str(binary)):
                self.assertEqual(DISCOVERY.executable([]), str(binary))
            with patch.object(DISCOVERY.platform, "system", return_value="FreeBSD"), patch.dict(DISCOVERY.os.environ, {}, clear=True), patch.object(DISCOVERY.shutil, "which", return_value=None):
                with self.assertRaisesRegex(RuntimeError, "no bundled tools target for FreeBSD"):
                    DISCOVERY.executable([])

    def test_oracle_root_fallback_precedes_host_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            suffix = ".exe" if DISCOVERY.os.name == "nt" else ""
            binary = root / "bin" / f"mwcc-tools{suffix}"
            binary.parent.mkdir()
            binary.write_bytes(b"test fixture")
            with patch.object(DISCOVERY.platform, "system", return_value="FreeBSD"), patch.dict(DISCOVERY.os.environ, {}, clear=True):
                self.assertEqual(DISCOVERY.executable(["--oracle-root", str(root)]), str(binary.resolve()))

    def test_macho_cpu_and_command_bounds(self):
        def macho(cpu):
            return bytearray(struct.pack("<IIIIIIIIII", 0xfeedfacf, cpu, 0, 2, 1, 8, 0, 0, 1, 8))
        for cpu, target in [(0x01000007, "x86_64-apple-darwin"), (0x0100000c, "aarch64-apple-darwin")]:
            data = macho(cpu)
            self.assertEqual(AUDIT.validate_native_binary(data, target)["format"], "mach_o")
            other = "aarch64-apple-darwin" if target.startswith("x86_64") else "x86_64-apple-darwin"
            with self.assertRaises(AssertionError):
                AUDIT.validate_native_binary(data, other)
            for damaged in [data[:-1], data[:36] + struct.pack("<I", 0), data[:36] + struct.pack("<I", 12)]:
                with self.assertRaises(AssertionError):
                    AUDIT.validate_native_binary(damaged, target)
        with self.assertRaises(AssertionError):
            AUDIT.validate_native_binary(b"\xca\xfe\xba\xbe" + bytes(64), "aarch64-apple-darwin")

    def test_pe_elf_and_cross_os_rejection(self):
        pe = bytearray(128)
        pe[:2] = b"MZ"
        struct.pack_into("<I", pe, 60, 64)
        pe[64:68] = b"PE\0\0"
        struct.pack_into("<H", pe, 68, 0x8664)
        struct.pack_into("<HHH", pe, 84, 2, 2, 0x20b)
        elf = bytearray(64)
        elf[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHI", elf, 16, 3, 62, 1)
        self.assertEqual(AUDIT.validate_native_binary(pe, "x86_64-pc-windows-msvc")["format"], "pe")
        self.assertEqual(AUDIT.validate_native_binary(elf, "x86_64-unknown-linux-gnu")["format"], "elf")
        for data, target in [(pe, "x86_64-unknown-linux-gnu"), (elf, "x86_64-apple-darwin"), (elf, "x86_64-pc-windows-msvc")]:
            with self.assertRaises(AssertionError):
                AUDIT.validate_native_binary(data, target)


if __name__ == "__main__":
    unittest.main()

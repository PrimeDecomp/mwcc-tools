"""Kit contract tests exercise shipped native artifacts, without Python oracle imports."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

KIT = Path(__file__).resolve().parents[1]


class NativeOracleTests(unittest.TestCase):
    def test_both_artifacts_and_source_pin_are_covered(self):
        native = json.loads((KIT / "native-manifest.json").read_text())
        for target, entry in native["artifacts"].items():
            archive = KIT / entry["path"]
            self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), entry["sha256"])
            with zipfile.ZipFile(archive) as bundle:
                self.assertIsNone(bundle.testzip())
                release = json.loads(bundle.read("release.json"))
                self.assertEqual(release["target"], target)
                self.assertEqual(release["source_revision"], native["source_revision"])
                self.assertEqual(release["protocols"], native["protocols"])
                self.assertEqual(release["executable_sha256"], entry["executable_sha256"])
                self.assertEqual(set(bundle.namelist()), set(release["files"]) | {"release.json"})
                for name, info in release["files"].items():
                    data = bundle.read(name)
                    self.assertEqual(len(data), info["bytes"])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), info["sha256"])
        source = KIT / native["source_bundle"]["path"]
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), native["source_bundle"]["sha256"])
        with tempfile.TemporaryDirectory(prefix="native source bundle ") as temporary:
            heads = subprocess.check_output(["git", "bundle", "list-heads", str(source)], text=True, cwd=temporary)
        self.assertIn(native["source_revision"], [line.split()[0] for line in heads.splitlines()])

    def test_installed_native_commands_and_errors(self):
        with tempfile.TemporaryDirectory(prefix="native oracle kit ") as temporary:
            project = Path(temporary)
            command = [sys.executable, str(KIT / "install-native.py"), str(project), "--apply"]
            first = subprocess.run(command, check=True, capture_output=True, text=True)
            executable = Path(next(line.removeprefix("MWCC_TOOLS=") for line in first.stdout.splitlines() if line.startswith("MWCC_TOOLS=")))
            repeat = subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertIn("0 file changes", repeat.stdout)
            version = subprocess.run([str(executable), "--version"], cwd=project, check=True, capture_output=True, text=True)
            self.assertIn("mwcc-tools 0.1.0", version.stdout)
            for compiler in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
                trace = KIT / f"tests/native-fixtures/{compiler}.trace"
                result = subprocess.run([str(executable), "oracle", "analyze-trace", str(trace),
                                         "--version", compiler, "--function", "pipeline", "--raw"],
                                        cwd=project, check=True, capture_output=True, text=True)
                self.assertEqual(result.stderr, "")
                report = json.loads(result.stdout)
                self.assertEqual(report["schema_version"], 6)
                self.assertTrue(report["functions"])
                self.assertTrue(report["raw"])
            invalid = project / "invalid.trace"
            invalid.write_text("ra.before function=unterminated\n")
            result = subprocess.run([str(executable), "oracle", "analyze-trace", str(invalid)],
                                    cwd=project, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr)
            self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()

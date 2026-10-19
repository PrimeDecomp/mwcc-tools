"""Notice regression checks using real Windows-style and Unix-style checkouts."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import capture_notices


class NoticeCheckoutTests(unittest.TestCase):
    def test_checkout_line_endings_do_not_change_workspace_notices(self):
        with tempfile.TemporaryDirectory(prefix="notice checkouts ") as directory:
            directory = Path(directory)
            source = directory / "source"
            source.mkdir()
            (source / ".gitattributes").write_bytes((capture_notices.ROOT / ".gitattributes").read_bytes())
            (source / "LICENSE").write_bytes(b"Workspace notice\nCopyright test\n")
            (source / "Cargo.lock").write_bytes(b"version = 4\n")

            def git(root, *arguments):
                return subprocess.check_output(
                    ["git", "-C", str(root), *arguments], stderr=subprocess.PIPE
                )

            git(source, "init", "--quiet")
            git(source, "add", ".")
            git(source, "-c", "user.name=Notice Test", "-c", "user.email=notice@example.invalid",
                "commit", "--quiet", "-m", "fixture")
            dependency = directory / "capstone-sys"
            (dependency / "capstone").mkdir(parents=True)
            (dependency / "capstone/LICENSE.TXT").write_bytes(b"Decoder notice\n")
            (dependency / "capstone/LICENSE_LLVM.TXT").write_bytes(b"LLVM notice\n")
            observed = []
            for autocrlf in ("true", "false"):
                checkout = directory / autocrlf
                subprocess.check_output(
                    ["git", "clone", "--quiet", "--no-checkout", str(source), str(checkout)],
                    stderr=subprocess.PIPE,
                )
                git(checkout, "-c", f"core.autocrlf={autocrlf}", "checkout", "--quiet", "HEAD")
                metadata = {
                    "workspace_members": ["workspace"],
                    "resolve": {"nodes": [{"id": "workspace"}, {"id": "decoder"}]},
                    "packages": [
                        {"id": "workspace", "name": "mwcc-tools", "version": "0.1.0",
                         "license": "CC0-1.0", "manifest_path": str(checkout / "Cargo.toml")},
                        {"id": "decoder", "name": "capstone-sys", "version": "0.0.0",
                         "license": "MIT", "manifest_path": str(dependency / "Cargo.toml"),
                         "source": "git+fixture"},
                    ],
                }
                with patch.object(capture_notices, "ROOT", checkout), patch.object(
                    capture_notices, "run", return_value=json.dumps(metadata)
                ):
                    packages, blocks, notices = capture_notices.collect("fixture-target")
                observed.append((packages, blocks, notices))
                self.assertEqual((checkout / "LICENSE").read_bytes(), b"Workspace notice\nCopyright test\n")
                self.assertEqual((checkout / "Cargo.lock").read_bytes(), b"version = 4\n")
            self.assertEqual(observed[0], observed[1])


if __name__ == "__main__":
    unittest.main()

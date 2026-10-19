"""The repository: its root, its versions, where things live, and how files in it are written.

Paths are anchored at the repository root, never the working directory, so every tool works from anywhere. Files are
written atomically (a temporary file in the same directory, then a rename), so a crash or Ctrl-C never leaves half a
file. Changes to the configuration go through a Transaction under the repository lock, so two tools running at once
cannot lose each other's edits: a file that changed since the transaction read it makes the commit fail instead.
"""

from __future__ import annotations

import contextlib
import hashlib
import os
import re
import sys
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# (the version every tool defaults to: the one the sources are written for)
PRIMARY = "GC_2_6"


class Error(Exception):
    """A problem a tool reports to its user and stops on (a bad argument, an invalid file, a stale input)."""


@dataclass(frozen=True)
class Version:
    """A compiler version this repository builds: its name (a config/ directory) and its VERSION index (version.h).
    A derived version (BASE set) has no original: it is BASE's sources and configuration built with its own VERSION,
    linked from the sources alone (equivalent.py --source-only), and nothing compares it."""

    name: str
    index: int
    base: str | None = None

    @property
    def config(self) -> Path:
        return ROOT / "config" / self.name

    @property
    def build(self) -> Path:
        return ROOT / "build" / self.name

    def __str__(self) -> str:
        return self.name


def _read_versions() -> tuple[Version, ...]:
    """The versions, in release order, from include/version.h's VERSION_<NAME> defines (the single list). Each needs a
    config directory of the same name (case aside: version.h spells GC_2_0p1 GC_2_0P1); a config.json with
    "derived_from" makes it a derived version of that one. A directory whose config.json names "source_version" is
    not one: it is another compiler's split, which the repository above builds from these sources as that version
    (GC_1_3_2 as GC_2_7), and these tools leave it alone."""
    header = (ROOT / "include/version.h").read_text()
    directories = {
        p.name.upper(): p.name
        for p in (ROOT / "config").iterdir()
        if (p / "config.json").exists() and '"source_version":' not in (p / "config.json").read_text()
    }
    versions = []
    for name, index in re.findall(r"^#define VERSION_(\w+) (\d+)$", header, re.M):
        if name.upper() not in directories:
            raise Error(f"include/version.h defines VERSION_{name}, but config/ has no directory for it")
        raw = (ROOT / "config" / directories[name.upper()] / "config.json").read_text()
        base = re.search(r'"derived_from": "(\w+)"', raw)
        versions.append(Version(directories[name.upper()], int(index), base[1] if base else None))
    if sorted(v.index for v in versions) != list(range(len(versions))):
        raise Error("include/version.h: the VERSION_* indices are not 0, 1, 2, ...")
    extra = set(directories.values()) - {v.name for v in versions}
    if extra:
        raise Error(f"config/ has {', '.join(sorted(extra))}, which include/version.h does not define")
    return tuple(sorted(versions, key=lambda v: v.index))


ALL_VERSIONS = _read_versions()
# (the versions of an original compiler, which the tools compare, split and relink; and the derived ones)
VERSIONS = tuple(v for v in ALL_VERSIONS if v.base is None)
DERIVED = tuple(v for v in ALL_VERSIONS if v.base is not None)


def version(name: str | Version | None = None) -> Version:
    """The version NAME (default: the primary one)."""
    if isinstance(name, Version):
        return name
    name = name or PRIMARY
    for v in ALL_VERSIONS:
        if v.name == name:
            return v
    raise Error(f"unknown version {name!r} (one of {', '.join(v.name for v in ALL_VERSIONS)})")


def relative(path: Path | str) -> str:
    """PATH relative to the root, with forward slashes: the form the configuration names files in (a path outside the
    repository stays absolute)."""
    path = Path(path)
    if path.is_absolute():
        resolved = path.resolve()
        path = resolved.relative_to(ROOT) if resolved.is_relative_to(ROOT) else resolved
    return path.as_posix()


def digest(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def write_atomic(path: Path | str, data: bytes | str) -> None:
    """Write DATA to PATH all at once: readers see the old file or the new one, never a part."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(temporary, path)
    except BaseException:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)
        raise


# The repository lock: one file, locked by whichever process changes the configuration. Re-entrant within a process.
_LOCK_PATH = ROOT / "build" / ".config.lock"
_lock_state = threading.local()


@contextlib.contextmanager
def locked():
    """Hold the repository lock (waiting for it). Every change to config/ is made while holding it."""
    depth = getattr(_lock_state, "depth", 0)
    if depth:
        _lock_state.depth += 1
        try:
            yield
        finally:
            _lock_state.depth -= 1
        return
    _LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(_LOCK_PATH, "a+b") as f:
        if sys.platform == "win32":
            import msvcrt

            f.seek(0)
            while True:
                try:
                    msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
                    break
                except OSError:  # (LK_LOCK gives up after 10 s: keep waiting)
                    continue
        else:
            import fcntl

            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        _lock_state.depth = 1
        try:
            yield
        finally:
            _lock_state.depth = 0
            if sys.platform == "win32":
                import msvcrt

                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


class StaleFile(Error):
    pass


class Transaction:
    """A set of file changes made all together or not at all.

    Read files through read() so the transaction knows what they held; stage new contents with write(); commit()
    then, under the repository lock, checks that nothing it read has changed since (another tool's edit would
    otherwise be lost) and writes every file. The project's C sources and headers are written as .clang-format lays
    them out (clang.format_source), so an edit computed as plain text needs no formatting pass. Files whose new
    contents equal the old are left alone (their modification times too, so ninja does not rebuild for nothing).

        with Transaction() as tx:
            rows = json.loads(tx.read("config/GC_2_6/functions.json"))
            ...
            tx.write("config/GC_2_6/functions.json", text)
        # (committed when the block ends without an exception; discarded otherwise)
    """

    def __init__(self):
        self.read_digests: dict[Path, str | None] = {}
        self.staged: dict[Path, bytes | None] = {}

    def _path(self, path) -> Path:
        path = Path(path)
        return path if path.is_absolute() else ROOT / path

    def read_bytes(self, path) -> bytes | None:
        """PATH's contents (as staged, if this transaction wrote it), or None if it does not exist."""
        path = self._path(path)
        if path in self.staged:
            return self.staged[path]
        try:
            data = path.read_bytes()
        except FileNotFoundError:
            data = None
        self.read_digests.setdefault(path, None if data is None else digest(data))
        return data

    def read(self, path) -> str:
        data = self.read_bytes(path)
        if data is None:
            raise Error(f"{relative(self._path(path))} does not exist")
        return data.decode()

    def unchanged(self, path, text: str) -> None:
        """Fail unless PATH still holds TEXT (a source, read as latin-1 before the transaction began, that an edit was
        computed from)."""
        data = self.read_bytes(path)
        if data is None or data.decode("latin-1") != text:
            raise StaleFile(f"{relative(self._path(path))} changed while this ran: nothing written")

    def write(self, path, data: bytes | str | None) -> None:
        """Stage PATH's new contents (None: delete it)."""
        path = self._path(path)
        if path not in self.read_digests:
            self.read_bytes(path)
        self.staged[path] = data.encode() if isinstance(data, str) else data

    def changes(self) -> dict[Path, bytes | None]:
        """The staged files whose contents differ from what they hold now."""
        changed = {}
        for path, data in self.staged.items():
            try:
                old = path.read_bytes()
            except FileNotFoundError:
                old = None
            if old != data:
                changed[path] = data
        return changed

    def commit(self) -> list[Path]:
        """Write the staged changes, sources laid out by .clang-format; the paths changed."""
        from concurrent.futures import ThreadPoolExecutor
        from . import clang

        sources = [p for p, data in self.staged.items() if data is not None and clang.formatted(p)]
        with ThreadPoolExecutor() as pool:
            for path, data in zip(sources, pool.map(lambda p: clang.format_source(p, self.staged[p]), sources)):
                self.staged[path] = data
        with locked():
            for path, expected in self.read_digests.items():
                try:
                    now = digest(path.read_bytes())
                except FileNotFoundError:
                    now = None
                if now != expected:
                    raise StaleFile(f"{relative(path)} changed while this tool was working: run it again")
            changed = self.changes()
            for path, data in changed.items():
                if data is None:
                    path.unlink()
                else:
                    write_atomic(path, data)
        self.staged.clear()
        return sorted(changed)

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind is None:
            self.commit()
        return False

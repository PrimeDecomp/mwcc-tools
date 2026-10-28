#!/bin/sh
# Installs mwcc-tools (Linux x64, macOS Apple Silicon): the latest release's package into ~/.local/share/mwcc-tools,
# with `mwcc-tools` and `mw-ninja` linked into ~/.local/bin and the Python package `mwcc_tools` importable.
#
#   curl -LsSf https://github.com/PrimeDecomp/mwcc-tools/releases/latest/download/install.sh | sh
#
# Run again, or `mwcc-tools self update`, to update. Settings (environment variables):
#   MWCC_TOOLS_HOME            the installation directory ($XDG_DATA_HOME/mwcc-tools, ~/.local/share/mwcc-tools)
#   XDG_BIN_HOME               the directory of the links (~/.local/bin)
#   MWCC_TOOLS_VERSION         a release, such as 0.4.6 (the latest)
#   MWCC_TOOLS_ARCHIVE         a downloaded mwcc-tools-VERSION-TARGET.zip to install, its .sha256 beside it
#   MWCC_TOOLS_NO_MODIFY_PATH  1: change no shell profile (they get ~/.local/bin where the PATH lacks it)
#   MWCC_TOOLS_NO_PYTHON       1: make no Python see the package
# (all in main, run on the last line: a download cut short runs nothing)
set -eu

main() {

repository=PrimeDecomp/mwcc-tools
home=${MWCC_TOOLS_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/mwcc-tools}
bin=${XDG_BIN_HOME:-$HOME/.local/bin}

say() { printf 'mwcc-tools: %s\n' "$*"; }
fail() { printf 'mwcc-tools: %s\n' "$*" >&2; exit 1; }

case "$(uname -s)-$(uname -m)" in
    Linux-x86_64 | Linux-amd64) target=x86_64-unknown-linux-gnu ;;
    Darwin-arm64 | Darwin-aarch64) target=aarch64-apple-darwin ;;
    Darwin-x86_64)
        # (a shell under Rosetta reports x86_64 on Apple Silicon)
        if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = 1 ]; then
            target=aarch64-apple-darwin
        else
            fail "there is no release for Intel Macs; build from source (BUILDING.md)"
        fi ;;
    MINGW* | MSYS* | CYGWIN*) fail "on Windows, run install.ps1 in PowerShell (README.md, Installation)" ;;
    *) fail "there is no release for $(uname -s) $(uname -m); build from source (BUILDING.md)" ;;
esac

download() { # URL FILE
    if command -v curl >/dev/null 2>&1; then
        curl --proto '=https' --tlsv1.2 -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --https-only -O "$2" "$1"
    else
        fail "curl or wget is needed"
    fi
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        fail "sha256sum or shasum is needed"
    fi
}

unpack() { # ZIP DIRECTORY
    if command -v unzip >/dev/null 2>&1; then
        unzip -q -o "$1" -d "$2"
    elif command -v python3 >/dev/null 2>&1; then
        python3 -m zipfile -e "$1" "$2"
    else
        fail "unzip or python3 is needed"
    fi
}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT INT TERM

if [ -n "${MWCC_TOOLS_ARCHIVE:-}" ]; then
    archive=$MWCC_TOOLS_ARCHIVE
    [ -f "$archive.sha256" ] || fail "$archive.sha256 is missing"
    cp "$archive.sha256" "$scratch/archive.zip.sha256"
else
    version=${MWCC_TOOLS_VERSION:-}
    if [ -z "$version" ]; then
        download "https://api.github.com/repos/$repository/releases/latest" "$scratch/latest.json"
        version=$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' "$scratch/latest.json" | head -n 1)
        [ -n "$version" ] || fail "cannot read the latest release's version"
    fi
    version=${version#v}
    url="https://github.com/$repository/releases/download/v$version/mwcc-tools-$version-$target.zip"
    say "downloading $url"
    archive=$scratch/archive.zip
    download "$url" "$archive"
    download "$url.sha256" "$scratch/archive.zip.sha256"
fi
expected=$(cut -d' ' -f1 <"$scratch/archive.zip.sha256")
actual=$(sha256 "$archive")
[ "$actual" = "$expected" ] || fail "the archive's SHA-256 is $actual, but its .sha256 gives $expected"

mkdir -p "$home"
home=$(cd "$home" && pwd)
staging=$(mktemp -d "$home/.install-XXXXXX")
trap 'rm -rf "$scratch" "$staging"' EXIT INT TERM
unpack "$archive" "$staging"
[ -f "$staging/bin/mwcc-tools" ] || fail "the archive has no bin/mwcc-tools"
grep -q "\"target\": \"$target\"" "$staging/release.json" || fail "the archive is not for $target"
# (each file renamed into place: a running mwcc-tools keeps its own)
(cd "$staging" && find . -type f) | while IFS= read -r file; do
    file=${file#./}
    mkdir -p "$(dirname "$home/$file")"
    mv -f "$staging/$file" "$home/$file"
done
chmod 755 "$home/bin/"*
installed=$("$home/bin/mwcc-tools" --version)
say "installed $installed in $home"

# (links, which the programs follow back to this installation: mw-ninja to its mwcc-tools, self update to its files)
mkdir -p "$bin"
bin=$(cd "$bin" && pwd)
for program in mwcc-tools mw-ninja; do
    [ -f "$home/bin/$program" ] || continue
    if [ -e "$bin/$program" ] && [ ! -L "$bin/$program" ]; then
        say "$bin/$program is not a link this installer made: left as it is (remove it to use $home/bin/$program)"
        continue
    fi
    ln -sf "$home/bin/$program" "$bin/$program"
done

case ":$PATH:" in
    *":$bin:"*) on_path=1 ;;
    *) on_path= ;;
esac
if [ -z "$on_path" ] && [ "${MWCC_TOOLS_NO_MODIFY_PATH:-0}" != 1 ]; then
    cat >"$home/env" <<EOF
# $bin on the PATH (from mwcc-tools' install.sh)
case ":\${PATH}:" in
    *":$bin:"*) ;;
    *) export PATH="$bin:\$PATH" ;;
esac
EOF
    line=". \"$home/env\""
    changed=
    for profile in "$HOME/.profile" "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.zshenv"; do
        case $profile in
            */.profile) ;;
            */.zshenv) command -v zsh >/dev/null 2>&1 || [ -f "$profile" ] || continue ;;
            *) [ -f "$profile" ] || continue ;;
        esac
        if ! grep -qsF "$line" "$profile"; then
            printf '\n%s\n' "$line" >>"$profile"
            changed="$changed $profile"
        fi
    done
    if [ -d "$HOME/.config/fish" ]; then
        mkdir -p "$HOME/.config/fish/conf.d"
        printf 'fish_add_path -g "%s"\n' "$bin" >"$HOME/.config/fish/conf.d/mwcc-tools.fish"
    fi
    [ -z "$changed" ] || say "added $bin to the PATH in$changed"
fi

# (the Python package: a .pth in the user's site-packages names $home/python, so it follows updates)
if [ "${MWCC_TOOLS_NO_PYTHON:-0}" != 1 ] && [ -d "$home/python" ]; then
    done_sites=
    for python in python3 python; do
        command -v "$python" >/dev/null 2>&1 || continue
        # (macOS' /usr/bin/python3 without the command line tools would ask to install them)
        if [ "$(uname -s)" = Darwin ] && [ "$(command -v "$python")" = /usr/bin/python3 ] && ! xcode-select -p >/dev/null 2>&1; then
            continue
        fi
        site=$("$python" -c 'import site, sys; print(site.getusersitepackages()) if site.ENABLE_USER_SITE and sys.version_info >= (3, 11) else sys.exit(1)' 2>/dev/null || true)
        [ -n "$site" ] || continue
        case "|$done_sites|" in *"|$site|"*) continue ;; esac
        done_sites="$done_sites|$site"
        if mkdir -p "$site" 2>/dev/null && printf '%s\n' "$home/python" >"$site/mwcc-tools.pth" 2>/dev/null; then
            say "$python imports mwcc_tools from $home/python"
        fi
    done
fi

case $on_path in
    1) say "run: mwcc-tools --help" ;;
    *)
        if [ -f "$home/env" ]; then
            say "open a new shell (or run: . \"$home/env\"), then: mwcc-tools --help"
        else
            say "add $bin to the PATH, then: mwcc-tools --help"
        fi ;;
esac
}

main "$@"

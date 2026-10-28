# Installs mwcc-tools (Windows x64): the latest release's package into %LOCALAPPDATA%\mwcc-tools, with `mwcc-tools` and
# `mw-ninja` on the PATH and the Python package `mwcc_tools` importable.
#
#   powershell -ExecutionPolicy ByPass -c "irm https://github.com/PrimeDecomp/mwcc-tools/releases/latest/download/install.ps1 | iex"
#
# Run again, or `mwcc-tools self update`, to update. Settings (environment variables):
#   MWCC_TOOLS_HOME            the installation directory (%LOCALAPPDATA%\mwcc-tools)
#   MWCC_TOOLS_VERSION         a release, such as 0.4.6 (the latest)
#   MWCC_TOOLS_ARCHIVE         a downloaded mwcc-tools-VERSION-TARGET.zip to install, its .sha256 beside it
#   MWCC_TOOLS_NO_MODIFY_PATH  1: leave the user's PATH as it is
#   MWCC_TOOLS_NO_PYTHON       1: make no Python see the package
# (Windows PowerShell 5.1 and PowerShell 7)

& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    $repository = 'PrimeDecomp/mwcc-tools'
    $target = 'x86_64-pc-windows-msvc'

    function Say($text) { Write-Host "mwcc-tools: $text" }

    if (-not [Environment]::Is64BitOperatingSystem) { throw 'mwcc-tools needs 64-bit Windows' }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $home_ = $env:MWCC_TOOLS_HOME
    if (-not $home_) { $home_ = Join-Path $env:LOCALAPPDATA 'mwcc-tools' }
    New-Item -ItemType Directory -Force $home_ | Out-Null
    $home_ = (Resolve-Path -LiteralPath $home_).ProviderPath
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ('mwcc-tools-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $scratch | Out-Null
    $staging = Join-Path $home_ ('.install-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    try {
        if ($env:MWCC_TOOLS_ARCHIVE) {
            $archive = $env:MWCC_TOOLS_ARCHIVE
            $sidecar = "$archive.sha256"
            if (-not (Test-Path -LiteralPath $sidecar)) { throw "$sidecar is missing" }
        } else {
            $version = $env:MWCC_TOOLS_VERSION
            if (-not $version) {
                $latest = Invoke-RestMethod -UseBasicParsing "https://api.github.com/repos/$repository/releases/latest"
                $version = $latest.tag_name
                if (-not $version) { throw "cannot read the latest release's version" }
            }
            $version = $version.TrimStart('v')
            $url = "https://github.com/$repository/releases/download/v$version/mwcc-tools-$version-$target.zip"
            Say "downloading $url"
            $archive = Join-Path $scratch 'archive.zip'
            $sidecar = "$archive.sha256"
            Invoke-WebRequest -UseBasicParsing $url -OutFile $archive
            Invoke-WebRequest -UseBasicParsing "$url.sha256" -OutFile $sidecar
        }
        $expected = ((Get-Content -LiteralPath $sidecar -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected) { throw "the archive's SHA-256 is $actual, but its .sha256 gives $expected" }

        # (Expand-Archive takes only .zip names)
        $zip = Join-Path $scratch 'package.zip'
        Copy-Item -LiteralPath $archive $zip
        Expand-Archive -LiteralPath $zip -DestinationPath $staging -Force
        $metadata = Get-Content -LiteralPath (Join-Path $staging 'release.json') -Raw | ConvertFrom-Json
        if ($metadata.target -ne $target) { throw "the archive is for $($metadata.target), not $target" }

        # (each file renamed into place; one in use, such as a running mwcc-tools.exe, is renamed aside first and
        # removed by the next update)
        # (its long, full name, as Get-ChildItem gives the files')
        $staged = (Get-Item -LiteralPath $staging).FullName.TrimEnd('\')
        Get-ChildItem -LiteralPath $staging -Recurse -File | ForEach-Object {
            $relative = $_.FullName.Substring($staged.Length + 1)
            $destination = Join-Path $home_ $relative
            New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
            if (Test-Path -LiteralPath $destination) {
                try {
                    Remove-Item -LiteralPath $destination -Force
                } catch {
                    for ($n = 0; ; $n++) {
                        $aside = "$destination.$n.mwcc-old"
                        if (Test-Path -LiteralPath $aside) {
                            try { Remove-Item -LiteralPath $aside -Force } catch { continue }
                        }
                        Move-Item -LiteralPath $destination $aside
                        break
                    }
                }
            }
            Move-Item -LiteralPath $_.FullName $destination
        }
    } finally {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    }
    $bin = Join-Path $home_ 'bin'
    $installed = & (Join-Path $bin 'mwcc-tools.exe') --version
    Say "installed $installed in $home_"

    if ($env:MWCC_TOOLS_NO_MODIFY_PATH -ne '1') {
        # (the registry's value as written, so the entries' %VARIABLES% stay unexpanded)
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
        $path = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $entries = @($path -split ';' | Where-Object { $_ })
        if (-not ($entries | Where-Object { $_.TrimEnd('\') -ieq $bin.TrimEnd('\') })) {
            $key.SetValue('Path', ((@($bin) + $entries) -join ';'), [Microsoft.Win32.RegistryValueKind]::ExpandString)
            # (setting a variable tells running programs, such as Explorer, that the environment changed)
            [Environment]::SetEnvironmentVariable('MWCC_TOOLS_INSTALLING', '1', 'User')
            [Environment]::SetEnvironmentVariable('MWCC_TOOLS_INSTALLING', $null, 'User')
            Say "added $bin to your PATH"
        }
        $key.Close()
        if (-not (($env:Path -split ';') | Where-Object { $_.TrimEnd('\') -ieq $bin.TrimEnd('\') })) {
            $env:Path = "$bin;$env:Path"
        }
    }

    # (the Python package: a .pth in each Python's (3.11 or later) user site-packages names <home>\python, so it
    # follows updates)
    if ($env:MWCC_TOOLS_NO_PYTHON -ne '1' -and (Test-Path -LiteralPath (Join-Path $home_ 'python'))) {
        $script = 'import site, sys; print(site.getusersitepackages()) if site.ENABLE_USER_SITE and sys.version_info >= (3, 11) else sys.exit(1)'
        $done = @()
        foreach ($python in @(@('py', '-3'), @('python'), @('python3'))) {
            if (-not (Get-Command $python[0] -ErrorAction SilentlyContinue)) { continue }
            $arguments = @($python | Select-Object -Skip 1) + @('-c', $script)
            try { $site = & $python[0] @arguments 2>$null } catch { continue }
            if ($LASTEXITCODE -ne 0 -or -not $site) { continue }
            $site = "$site".Trim()
            if ($done -contains $site) { continue }
            $done += $site
            New-Item -ItemType Directory -Force $site | Out-Null
            [IO.File]::WriteAllText((Join-Path $site 'mwcc-tools.pth'), (Join-Path $home_ 'python') + "`n")
            Say "Python ($($python -join ' ')) imports mwcc_tools from $(Join-Path $home_ 'python')"
        }
    }

    if (($env:Path -split ';') | Where-Object { $_.TrimEnd('\') -ieq $bin.TrimEnd('\') }) {
        Say 'run: mwcc-tools --help (other open terminals see the PATH once restarted)'
    } else {
        Say "add $bin to the PATH, then: mwcc-tools --help"
    }
}

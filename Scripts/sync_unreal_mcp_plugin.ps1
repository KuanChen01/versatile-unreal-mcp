<#
.SYNOPSIS
  Sync / upgrade the UnrealMCP editor plugin into a target Unreal project.

.DESCRIPTION
  Reduces copy-drift between:
    - canonical source:  <repo>/MCPGameProject/Plugins/UnrealMCP
    - prebuilt releases: <repo>/Releases/UnrealMCP/...
    - per-project copies: <YourProject>/Plugins/UnrealMCP

  Modes:
    Source  - Sync Source + .uplugin (+ Config). Strips Intermediate/Binaries from destination first for those trees.
              Use when you will rebuild the plugin against the target project/engine.
    Release - Expand a UE_x.y prebuilt package (zip or unpacked Releases tree) into the target.
              Use for install-without-source-build on matching engine versions.
    Status  - Compare target plugin vs source (and optional release) and print a drift report.

  IMPORTANT:
    - Close Unreal Editor (or at least unload the project) before syncing Binaries, or copy may fail / leave stale DLLs.
    - After Source sync: generate project files if needed, rebuild Editor target, full restart Editor.
    - After Release sync: enable plugin if needed, full restart Editor.
    - Python MCP server must still match protocol 2.0 (upgrade plugin + Python together).

.PARAMETER TargetProject
  Path to a .uproject file or the project root directory that contains Plugins/.

.PARAMETER Mode
  Source | Release | Status  (default: Source)

.PARAMETER RepoRoot
  versatile-unreal-mcp repository root. Default: parent of this Scripts/ folder.

.PARAMETER EngineVersion
  For Release mode: 5.5 | 5.6 | 5.7 (also accepts UE_5.7). Default: 5.7

.PARAMETER ReleaseZip
  Optional explicit path to UnrealMCP-UE_x.y-Win64.zip. Overrides EngineVersion zip lookup.

.PARAMETER WhatIf
  Dry-run: show planned actions without writing.

.PARAMETER Force
  Overwrite destination even if it looks newer (still never touches unrelated project files).

.EXAMPLE
  # Sync canonical source into OW project (then rebuild Editor)
  .\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "E:\Kuan\Projects\Unreal\OW_CarScene_57\OW_CarScene_57.uproject" -Mode Source

.EXAMPLE
  # Install prebuilt 5.7 package into a clean project
  .\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "E:\Kuan\Projects\Unreal\UnrealMCP_ZipSmoke_57" -Mode Release -EngineVersion 5.7

.EXAMPLE
  # Drift report only
  .\Scripts\sync_unreal_mcp_plugin.ps1 -TargetProject "E:\...\MyGame.uproject" -Mode Status
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string] $TargetProject,

    [ValidateSet("Source", "Release", "Status")]
    [string] $Mode = "Source",

    [string] $RepoRoot = "",

    [string] $EngineVersion = "5.7",

    [string] $ReleaseZip = "",

    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info([string] $Msg) { Write-Host "[sync] $Msg" -ForegroundColor Cyan }
function Write-Ok([string] $Msg) { Write-Host "[sync] $Msg" -ForegroundColor Green }
function Write-Warn([string] $Msg) { Write-Host "[sync] $Msg" -ForegroundColor Yellow }
function Write-Err([string] $Msg) { Write-Host "[sync] $Msg" -ForegroundColor Red }

function Resolve-RepoRoot {
    param([string] $Explicit)
    if ($Explicit) {
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    $here = $PSScriptRoot
    if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Path }
    return (Resolve-Path -LiteralPath (Join-Path $here "..")).Path
}

function Resolve-ProjectRoot {
    param([string] $Target)
    $p = $Target.Trim().Trim('"')
    if (-not (Test-Path -LiteralPath $p)) {
        throw "Target path not found: $p"
    }
    $item = Get-Item -LiteralPath $p
    if ($item.PSIsContainer) {
        $uprojects = @(Get-ChildItem -LiteralPath $item.FullName -Filter "*.uproject" -File -ErrorAction SilentlyContinue)
        if ($uprojects.Count -eq 0) {
            # Allow bare project folder without uproject for packaging sandboxes
            Write-Warn "No .uproject under $($item.FullName); treating as project root anyway."
        }
        return $item.FullName
    }
    if ($item.Extension -ne ".uproject") {
        throw "Target must be a .uproject file or project directory: $p"
    }
    return $item.Directory.FullName
}

function Get-FileSha256([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

function Get-RelativeFileList {
    param(
        [string] $Root,
        [string[]] $IncludeGlobs
    )
    $files = @()
    foreach ($g in $IncludeGlobs) {
        $files += Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $g -ErrorAction SilentlyContinue
    }
    # Prefer explicit Source + uplugin + Config walk
    return $files
}

function Get-PluginFingerprint {
    param([string] $PluginDir)
    $result = [ordered]@{
        exists       = $false
        uplugin      = $null
        source_files = 0
        has_binaries = $false
        sample_hashes = @{}
        stamp        = $null
    }
    if (-not (Test-Path -LiteralPath $PluginDir)) { return $result }
    $result.exists = $true
    $uplugin = Join-Path $PluginDir "UnrealMCP.uplugin"
    if (Test-Path -LiteralPath $uplugin) {
        $result.uplugin = Get-FileSha256 $uplugin
    }
    $src = Join-Path $PluginDir "Source"
    if (Test-Path -LiteralPath $src) {
        $all = Get-ChildItem -LiteralPath $src -Recurse -File -ErrorAction SilentlyContinue
        $result.source_files = @($all).Count
        $keyFiles = @(
            "Source\UnrealMCP\Private\UnrealMCPBridge.cpp",
            "Source\UnrealMCP\Private\MCPServerRunnable.cpp",
            "Source\UnrealMCP\Private\Commands\UnrealMCPEditorCommands.cpp",
            "Source\UnrealMCP\Private\Commands\UnrealMCPBlueprintNodeCommands.cpp",
            "Source\UnrealMCP\UnrealMCP.Build.cs",
            "UnrealMCP.uplugin"
        )
        foreach ($rel in $keyFiles) {
            $full = Join-Path $PluginDir $rel
            $result.sample_hashes[$rel] = Get-FileSha256 $full
        }
    }
    $dll = Join-Path $PluginDir "Binaries\Win64\UnrealEditor-UnrealMCP.dll"
    $result.has_binaries = Test-Path -LiteralPath $dll
    if ($result.has_binaries) {
        $result.dll_hash = Get-FileSha256 $dll
    } else {
        $result.dll_hash = $null
    }
    $stampPath = Join-Path $PluginDir "UnrealMCP.sync.json"
    if (Test-Path -LiteralPath $stampPath) {
        try { $result.stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json } catch { $result.stamp = $null }
    }
    return $result
}

function Write-SyncStamp {
    param(
        [string] $PluginDir,
        [string] $ModeName,
        [string] $Repo,
        [string] $SourceLabel
    )
    $git = $null
    try {
        $git = (& git -C $Repo rev-parse --short HEAD 2>$null)
        if ($LASTEXITCODE -ne 0) { $git = $null }
    } catch { $git = $null }

    $stamp = [ordered]@{
        synced_at_utc   = (Get-Date).ToUniversalTime().ToString("o")
        mode            = $ModeName
        source          = $SourceLabel
        repo_root       = $Repo
        git_commit      = $git
        protocol_hint   = "2.0"
        notes           = "Written by Scripts/sync_unreal_mcp_plugin.ps1. Restart Unreal Editor after sync. Rebuild if Mode=Source."
    }
    $path = Join-Path $PluginDir "UnrealMCP.sync.json"
    ($stamp | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $path -Encoding UTF8
    Write-Info "Wrote stamp $path"
}

function Sync-Directory {
    param(
        [string] $From,
        [string] $To,
        [string[]] $ExcludeDirs = @()
    )
    if (-not (Test-Path -LiteralPath $From)) {
        throw "Source path missing: $From"
    }
    if ($WhatIfPreference -or $PSCmdlet.ShouldProcess($To, "Mirror from $From")) {
        if ($WhatIfPreference) {
            Write-Info "WhatIf: would mirror $From -> $To (exclude: $($ExcludeDirs -join ', '))"
            return
        }
        New-Item -ItemType Directory -Force -Path $To | Out-Null
        # robocopy /MIR is destructive for extras under To — only use under controlled plugin subfolders we own
        $xd = @()
        foreach ($d in $ExcludeDirs) { $xd += @("/XD", $d) }
        $args = @($From, $To, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/nc", "/ns", "/np") + $xd
        & robocopy @args | Out-Null
        $code = $LASTEXITCODE
        # robocopy: 0-7 success-ish
        if ($code -ge 8) {
            throw "robocopy failed from $From to $To (exit $code)"
        }
        $global:LASTEXITCODE = 0
    }
}

function Remove-PluginBuildArtifacts {
    param([string] $PluginDir)
    foreach ($rel in @("Intermediate", "Binaries", "Saved")) {
        $p = Join-Path $PluginDir $rel
        if (Test-Path -LiteralPath $p) {
            if ($WhatIfPreference) {
                Write-Info "WhatIf: would remove $p"
            } else {
                Write-Info "Removing $p"
                Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

# --- main ---
$Repo = Resolve-RepoRoot -Explicit $RepoRoot
$ProjectRoot = Resolve-ProjectRoot -Target $TargetProject
$DestPlugin = Join-Path $ProjectRoot "Plugins\UnrealMCP"
$Canonical = Join-Path $Repo "MCPGameProject\Plugins\UnrealMCP"

Write-Info "RepoRoot     = $Repo"
Write-Info "ProjectRoot  = $ProjectRoot"
Write-Info "DestPlugin   = $DestPlugin"
Write-Info "Mode         = $Mode"

if (-not (Test-Path -LiteralPath $Canonical)) {
    throw "Canonical plugin missing: $Canonical"
}

# Normalize engine version string
$EngineVersion = $EngineVersion.Trim()
if ($EngineVersion -match '^UE_?(5\.\d)$') { $EngineVersion = $Matches[1] }
if ($EngineVersion -match '^(5\.\d)$') { $ueTag = "UE_$EngineVersion" } else { $ueTag = "UE_$EngineVersion" }

switch ($Mode) {
    "Status" {
        $srcFp = Get-PluginFingerprint -PluginDir $Canonical
        $dstFp = Get-PluginFingerprint -PluginDir $DestPlugin
        Write-Host ""
        Write-Host "=== UnrealMCP drift report ===" -ForegroundColor White
        Write-Host ("Canonical source files : {0}" -f $srcFp.source_files)
        Write-Host ("Target exists          : {0}" -f $dstFp.exists)
        if ($dstFp.exists) {
            Write-Host ("Target source files    : {0}" -f $dstFp.source_files)
            Write-Host ("Target has binaries    : {0}" -f $dstFp.has_binaries)
            if ($dstFp.has_binaries) {
                Write-Host ("Target DLL sha256      : {0}" -f $dstFp.dll_hash)
            }
            if ($dstFp.stamp) {
                Write-Host ("Last sync stamp        : {0} mode={1} git={2}" -f $dstFp.stamp.synced_at_utc, $dstFp.stamp.mode, $dstFp.stamp.git_commit)
            } else {
                Write-Host "Last sync stamp        : (none - not installed by this script, or stamp missing)"
            }
            Write-Host ""
            Write-Host "Source/uplugin hash compare (canonical vs target):"
            $mismatches = 0
            foreach ($k in $srcFp.sample_hashes.Keys) {
                $a = $srcFp.sample_hashes[$k]
                $b = $dstFp.sample_hashes[$k]
                $same = ($a -and $b -and ($a -eq $b))
                if (-not $same) { $mismatches++ }
                $mark = if ($same) { "OK  " } else { "DIFF" }
                Write-Host ("  [{0}] {1}" -f $mark, $k)
                if (-not $same) {
                    Write-Host ("         src={0}" -f $(if ($a) { $a.Substring(0,12) + "..." } else { "missing" }))
                    Write-Host ("         dst={0}" -f $(if ($b) { $b.Substring(0,12) + "..." } else { "missing" }))
                }
            }
            Write-Host ""
            Write-Host "Note: Editor-built DLLs legitimately differ from repo/sample Binaries; Source match is what Status judges."
            Write-Host ""
            if ($mismatches -eq 0 -and $dstFp.source_files -gt 0) {
                Write-Ok "Target Source matches canonical on sampled files."
                exit 0
            } else {
                Write-Warn "Target Source drifts from canonical ($mismatches sampled file(s) differ). Consider -Mode Source or -Mode Release."
                exit 1
            }
        } else {
            Write-Warn "Target plugin not installed. Run -Mode Source or -Mode Release."
            exit 1
        }
    }

    "Source" {
        Write-Info "Syncing canonical SOURCE (no prebuilt DLL) -> target"
        Write-Warn "Close the Editor if it has this project open (DLL locks / stale Intermediate)."
        if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "Plugins"))) {
            if ($WhatIfPreference) { Write-Info "WhatIf: would create Plugins/" }
            else { New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "Plugins") | Out-Null }
        }

        if ((Test-Path -LiteralPath $DestPlugin) -and -not $Force) {
            Write-Info "Destination exists; will update in place (Source tree + uplugin + Config)."
        }

        # Ensure plugin root exists
        if (-not $WhatIfPreference) {
            New-Item -ItemType Directory -Force -Path $DestPlugin | Out-Null
        }

        # Copy Source
        Sync-Directory -From (Join-Path $Canonical "Source") -To (Join-Path $DestPlugin "Source")

        # uplugin
        $upFrom = Join-Path $Canonical "UnrealMCP.uplugin"
        $upTo = Join-Path $DestPlugin "UnrealMCP.uplugin"
        if ($WhatIfPreference) { Write-Info "WhatIf: copy $upFrom -> $upTo" }
        else { Copy-Item -LiteralPath $upFrom -Destination $upTo -Force }

        # Config if present
        $cfgFrom = Join-Path $Canonical "Config"
        if (Test-Path -LiteralPath $cfgFrom) {
            Sync-Directory -From $cfgFrom -To (Join-Path $DestPlugin "Config")
        }

        # Clear build products so next Editor build is clean against this project
        Remove-PluginBuildArtifacts -PluginDir $DestPlugin

        if (-not $WhatIfPreference) {
            Write-SyncStamp -PluginDir $DestPlugin -ModeName "Source" -Repo $Repo -SourceLabel $Canonical
        }

        Write-Ok "Source sync complete."
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor White
        Write-Host "  1. Close Unreal Editor if still open."
        Write-Host "  2. Right-click the .uproject -> Generate Visual Studio project files (if needed)."
        Write-Host "  3. Build the Editor target (Development)."
        Write-Host "  4. Open the project (full restart preferred after plugin changes)."
        Write-Host "  5. Confirm: get_bridge_status -> protocol_version 2.0"
        Write-Host "  6. Keep Python server from this repo in sync (protocol 2.0)."
        exit 0
    }

    "Release" {
        $releasesDir = Join-Path $Repo "Releases\UnrealMCP"
        $zipPath = $ReleaseZip
        if (-not $zipPath) {
            $zipPath = Join-Path $releasesDir "UnrealMCP-$ueTag-Win64.zip"
        }
        $unpacked = Join-Path $releasesDir "$ueTag\UnrealMCP"

        $sourceLabel = $null
        $stageDir = $null
        $cleanupStage = $false

        if ($WhatIfPreference) {
            if ($zipPath -and (Test-Path -LiteralPath $zipPath)) {
                Write-Info "WhatIf: would expand $zipPath and install into $DestPlugin"
            } elseif (Test-Path -LiteralPath (Join-Path $unpacked "UnrealMCP.uplugin")) {
                Write-Info "WhatIf: would copy $unpacked -> $DestPlugin"
            } else {
                Write-Warn "WhatIf: no release package found for $ueTag (zip or unpacked)."
            }
            Write-Ok "Release WhatIf complete ($ueTag)."
            exit 0
        }

        if ($zipPath -and (Test-Path -LiteralPath $zipPath)) {
            Write-Info "Using release zip: $zipPath"
            $stageDir = Join-Path $env:TEMP ("UnrealMCP_sync_release_" + [guid]::NewGuid().ToString("N"))
            New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
            Expand-Archive -LiteralPath $zipPath -DestinationPath $stageDir -Force
            $candidate = Join-Path $stageDir "UnrealMCP"
            if (-not (Test-Path -LiteralPath (Join-Path $candidate "UnrealMCP.uplugin"))) {
                throw "Zip did not contain UnrealMCP/UnrealMCP.uplugin"
            }
            $sourceLabel = $zipPath
            $cleanupStage = $true
            $fromPlugin = $candidate
        } elseif (Test-Path -LiteralPath (Join-Path $unpacked "UnrealMCP.uplugin")) {
            Write-Info "Using unpacked release tree: $unpacked"
            $fromPlugin = $unpacked
            $sourceLabel = $unpacked
        } else {
            throw "No release package found for $ueTag. Looked for zip '$zipPath' and unpacked '$unpacked'."
        }

        Write-Warn "Close the Editor if it has this project open before replacing Binaries."
        if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "Plugins"))) {
            New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "Plugins") | Out-Null
        }

        if (Test-Path -LiteralPath $DestPlugin) {
            Write-Info "Removing existing $DestPlugin for clean Release install"
            Remove-Item -LiteralPath $DestPlugin -Recurse -Force
        }

        New-Item -ItemType Directory -Force -Path (Split-Path $DestPlugin -Parent) | Out-Null
        Copy-Item -LiteralPath $fromPlugin -Destination $DestPlugin -Recurse -Force
        # Do not ship Intermediate from accidental copies
        $inter = Join-Path $DestPlugin "Intermediate"
        if (Test-Path -LiteralPath $inter) { Remove-Item -LiteralPath $inter -Recurse -Force -ErrorAction SilentlyContinue }
        Write-SyncStamp -PluginDir $DestPlugin -ModeName "Release" -Repo $Repo -SourceLabel $sourceLabel

        if ($cleanupStage -and $stageDir -and (Test-Path -LiteralPath $stageDir)) {
            Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
        }

        Write-Ok "Release sync complete ($ueTag)."
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor White
        Write-Host "  1. Open the .uproject in the matching Unreal Engine $EngineVersion."
        Write-Host "  2. Enable UnrealMCP if prompted; full restart once."
        Write-Host "  3. Run Python MCP server from this repo (protocol 2.0)."
        Write-Host "  4. Confirm: get_bridge_status -> protocol_version 2.0"
        exit 0
    }
}

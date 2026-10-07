# Synchronize Git repositories without requiring a Python installation.
# Compatible with Windows PowerShell 5.1 and PowerShell 7.
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Join-Path $HOME 'Workspace'),
    [string]$Manifest,
    [switch]$AuditOnly
)

function Run-Git {
    param([string]$Directory, [string[]]$Arguments)
    $commandArguments = @('-c', 'core.longpaths=true')
    if ($Directory) { $commandArguments += @('-C', $Directory) }
    $commandArguments += $Arguments
    # Git writes normal progress to stderr. In Windows PowerShell 5.1, capture
    # those records without treating every successful clone as a script error.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $messages = @(& $WorkspaceGitBinary @commandArguments 2>&1)
        $gitExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    $message = (($messages | ForEach-Object { $_.ToString() }) -join "`n").Trim()
    if ($gitExit -ne 0) {
        if (!$message) { $message = "Git exited $gitExit" }
        throw $message
    }
    return $message
}

function Assert-RelativePath {
    param($Value)
    if ($Value -isnot [string] -or !$Value) { throw "Invalid workspace path: $Value" }
    $parts = @($Value -split '/')
    if ($Value.Contains('\') -or $parts[0] -cnotin @('SDK', 'Product', 'Service')) {
        throw "Invalid workspace path: $Value"
    }
    foreach ($part in $parts) {
        if (!$part -or $part -in @('.', '..') -or $part -match '[<>:"\|?*\x00-\x1f]' -or
            $part.EndsWith(' ') -or $part.EndsWith('.') -or
            $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
            throw "Invalid Windows path: $Value"
        }
    }
}

function Assert-NoReparsePoint {
    param([string]$Path)
    $cursor = [System.IO.Path]::GetFullPath($Path)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point destination is not supported: $cursor"
            }
        }
        $parent = [System.IO.Directory]::GetParent($cursor)
        if ($null -eq $parent) { break }
        $cursor = $parent.FullName
    }
}

function Get-Destination {
    param([string]$Root, [string]$RelativePath)
    Assert-RelativePath $RelativePath
    $target = $Root
    foreach ($part in ($RelativePath -split '/')) { $target = Join-Path $target $part }
    $target = [System.IO.Path]::GetFullPath($target)
    Assert-NoReparsePoint $target
    return $target
}

function Get-CanonicalUrl {
    param([string]$Value)
    $value = $Value -replace '^git@github\.com:', 'https://github.com/'
    $value = $value -replace '^ssh://git@github\.com/', 'https://github.com/'
    $value = $value.TrimEnd('/') -replace '\.git$', ''
    if ($value.StartsWith('https://github.com/', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $value.ToLowerInvariant()
    }
    return $value
}

function Test-ExistingContainer {
    param([string]$Target, [string]$RelativePath, [string[]]$KnownPaths)
    foreach ($entry in @(Get-ChildItem -LiteralPath $Target -Force)) {
        if (!$entry.PSIsContainer -or
            ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { return $false }
        $name = $RelativePath + '/' + $entry.Name
        $known = $false
        foreach ($path in $KnownPaths) {
            if ($path -ceq $name -or $path.StartsWith($name + '/', [System.StringComparison]::Ordinal)) {
                $known = $true
                break
            }
        }
        if (!$known) { return $false }
    }
    return $true
}

function Sync-Repository {
    param([string]$Root, $Row, [string[]]$KnownPaths, [bool]$Audit)
    $target = Get-Destination $Root $Row.path
    $url = $Row.url
    $branch = $Row.branch
    if (!(Test-Path -LiteralPath (Join-Path $target '.git'))) {
        if ($Audit) { throw 'Checkout is missing' }
        if ($Row.lfs) { $null = Run-Git $null @('lfs', 'version') }
        $null = [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target))
        if ((Test-Path -LiteralPath $target) -and @(Get-ChildItem -LiteralPath $target -Force).Count -gt 0) {
            if (!(Test-ExistingContainer $target $Row.path $KnownPaths)) {
                throw 'Destination contains existing files without its own Git repository'
            }
            $null = Run-Git $target @('init', '-b', $branch)
            $null = Run-Git $target @('config', 'core.autocrlf', 'false')
            $null = Run-Git $target @('remote', 'add', 'origin', $url)
            $null = Run-Git $target @('fetch', '--tags', 'origin')
            $null = Run-Git $target @('checkout', '--track', '-b', $branch, "origin/$branch")
        } else {
            $null = Run-Git $null @('clone', '--config', 'core.autocrlf=false', '--config',
                                   'core.longpaths=true', '--branch', $branch, '--', $url, $target)
        }
    }
    $actualRoot = [System.IO.Path]::GetFullPath((Run-Git $target @('rev-parse', '--show-toplevel')))
    $comparison = [System.StringComparison]::Ordinal
    if ([System.IO.Path]::DirectorySeparatorChar -eq '\') {
        $comparison = [System.StringComparison]::OrdinalIgnoreCase
    }
    if (![string]::Equals($actualRoot, $target, $comparison)) {
        throw 'Destination is not an independent Git repository'
    }
    $origin = Run-Git $target @('remote', 'get-url', 'origin')
    if ((Get-CanonicalUrl $origin) -cne (Get-CanonicalUrl $url)) {
        throw 'Existing origin differs from the workspace manifest'
    }
    if ((Run-Git $target @('branch', '--show-current')) -cne $branch) {
        throw "Existing branch differs from $branch; local work was preserved"
    }
    if (Run-Git $target @('status', '--porcelain', '--untracked-files=all')) {
        throw 'Checkout has uncommitted changes; local work was preserved'
    }
    if (!$Audit) {
        $null = Run-Git $target @('config', 'core.longpaths', 'true')
        $null = Run-Git $target @('fetch', '--prune', '--tags', 'origin')
    }
    $reference = "refs/remotes/origin/$branch"
    $advertisement = Run-Git $target @('ls-remote', '--exit-code', 'origin', "refs/heads/$branch")
    $remoteHead = ($advertisement -split '\s+')[0]
    if ($remoteHead -notmatch '^[0-9a-f]{40}$') { throw 'Git did not return a valid remote commit' }
    if (!$Audit -and (Run-Git $target @('rev-parse', $reference)) -cne $remoteHead) {
        throw 'Remote changed during fetch; rerun to synchronize the new commit'
    }
    if ($Row.sourceCommit) {
        $null = Run-Git $target @('merge-base', '--is-ancestor', $Row.sourceCommit, $remoteHead)
    }
    if (!$Audit) {
        $null = Run-Git $target @('merge', '--ff-only', $reference)
        $null = Run-Git $target @('branch', "--set-upstream-to=origin/$branch", $branch)
        if ($Row.lfs) {
            $null = Run-Git $target @('lfs', 'install', '--local')
            $null = Run-Git $target @('lfs', 'pull')
            $null = Run-Git $target @('lfs', 'fsck')
        }
        if (Test-Path -LiteralPath (Join-Path $target '.gitmodules')) {
            $null = Run-Git $target @('submodule', 'update', '--init', '--recursive')
        }
    }
    $head = Run-Git $target @('rev-parse', 'HEAD')
    if ($head -cne $remoteHead) {
        throw "Local HEAD $head differs from GitHub $remoteHead; local commits were preserved"
    }
    if (Run-Git $target @('status', '--porcelain', '--untracked-files=all')) {
        throw 'Checkout is not clean after synchronization'
    }
    return [ordered]@{ path = $Row.path; directory = $target; branch = $branch;
        head = $head; remoteHead = $remoteHead; verified = $true;
        snapshotExact = (!$Row.sourceCommit -or $head -ceq $Row.sourceCommit) }
}

$previousErrorPreference = $ErrorActionPreference
$previousGitPrompt = $env:GIT_TERMINAL_PROMPT
try {
    $ErrorActionPreference = 'Stop'
    $env:GIT_TERMINAL_PROMPT = '0'
    $WorkspaceGitBinary = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $WorkspaceRoot = [System.IO.Path]::GetFullPath($WorkspaceRoot)
    Assert-NoReparsePoint $WorkspaceRoot
    if (!$Manifest) { $Manifest = Join-Path (Join-Path $WorkspaceRoot 'SDK') 'workspace-repositories.json' }
    $data = Get-Content -Raw -Encoding UTF8 -LiteralPath $Manifest | ConvertFrom-Json
    if ($data.version -ne 1 -or !$data.repositories) {
        throw 'Expected workspace manifest version 1 with repositories'
    }
    $seen = @{}
    foreach ($row in @($data.repositories)) {
        Assert-RelativePath $row.path
        if ($seen.ContainsKey($row.path)) { throw "Duplicate workspace path: $($row.path)" }
        $seen[$row.path] = $true
        if ($row.url -isnot [string] -or !$row.url -or $row.url.StartsWith('-')) {
            throw "Invalid Git remote: $($row.path)"
        }
        if ($row.branch -isnot [string] -or !$row.branch -or $row.branch.StartsWith('-')) {
            throw "Invalid Git branch: $($row.path)"
        }
        $null = Run-Git $null @('check-ref-format', "refs/heads/$($row.branch)")
        if ($null -ne $row.sourceCommit -and $row.sourceCommit -cnotmatch '^[0-9a-f]{40}$') {
            throw "Invalid source commit: $($row.path)"
        }
    }
    $directories = @()
    if ($null -ne $data.directories) { $directories = @($data.directories) }
    foreach ($name in $directories) { Assert-RelativePath $name }
    $knownPaths = @($data.repositories | ForEach-Object { $_.path }) + $directories
    $rows = @($data.repositories | Sort-Object @{ Expression = { ($_.path -split '/').Count } }, path)
    $report = [ordered]@{ timestamp = [DateTime]::UtcNow.ToString('o'); root = $WorkspaceRoot;
        auditOnly = [bool]$AuditOnly; repositories = @(); directoryErrors = @() }
    foreach ($row in $rows) {
        Write-Host "SYNC $($row.path)"
        try {
            $result = Sync-Repository $WorkspaceRoot $row $knownPaths ([bool]$AuditOnly)
            Write-Host "VERIFIED $($row.path)"
        } catch {
            $result = [ordered]@{ path = $row.path; verified = $false; error = $_.Exception.Message }
            Write-Host "FAILED $($row.path): $($result.error)"
        }
        $report.repositories += $result
    }
    foreach ($name in $directories) {
        try {
            $target = Get-Destination $WorkspaceRoot $name
            if ($AuditOnly) {
                if (!(Test-Path -LiteralPath $target -PathType Container)) { throw 'Directory is missing' }
            } else {
                $null = [System.IO.Directory]::CreateDirectory($target)
            }
        } catch {
            $report.directoryErrors += [ordered]@{ path = $name; error = $_.Exception.Message }
        }
    }
    $report.verified = @($report.repositories | Where-Object { $_.verified }).Count
    $report.total = $rows.Count
    $report.complete = ($report.verified -eq $report.total -and $report.directoryErrors.Count -eq 0)
    $output = Join-Path (Join-Path $WorkspaceRoot 'build') 'workspace-sync'
    Assert-NoReparsePoint $output
    $null = [System.IO.Directory]::CreateDirectory($output)
    $reportPath = Join-Path $output 'report.json'
    Assert-NoReparsePoint $reportPath
    $json = $report | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($reportPath, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "Verified $($report.verified)/$($report.total); report: $reportPath"
    if (!$report.complete) { throw "Workspace synchronization is incomplete. Review $reportPath" }
} finally {
    $ErrorActionPreference = $previousErrorPreference
    $env:GIT_TERMINAL_PROMPT = $previousGitPrompt
}

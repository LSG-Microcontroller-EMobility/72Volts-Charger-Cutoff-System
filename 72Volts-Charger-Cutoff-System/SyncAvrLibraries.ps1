# This script is installed beside GenerateGitVersion.bat. Avoid command-line
# arguments: Visual Micro's prebuild command processing may drop a trailing
# parameter. Resolve both paths from the script's actual directory instead.
$SketchPath = $PSScriptRoot
$GeneratedHeader = Join-Path $SketchPath 'GitVersion.generated.tmp'
$ErrorActionPreference = 'Stop'
$localLibraryLinkError = 'GPM_AVR_LOCAL_LIBRARY_LINK_ERROR'

function Invoke-Git([string]$Location, [string[]]$Arguments) {
    $value = & git -C $Location @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed in $($Location): git $($Arguments -join ' ')`n$($value -join "`n")"
    }
    return ($value -join "`n").Trim()
}
# Read the live official branch tip without fetching objects or changing refs.
# The timeout and credential settings keep unattended builds from waiting for input.
function get_remote_library_revision([string]$library_root) {
    $git_process = $null
    try {
        $start_info = New-Object System.Diagnostics.ProcessStartInfo
        $start_info.FileName = 'git'
        $start_info.Arguments = '-c core.askPass= -c credential.interactive=false ls-remote --symref origin HEAD refs/heads/main refs/heads/master'
        $start_info.WorkingDirectory = $library_root
        $start_info.UseShellExecute = $false
        $start_info.CreateNoWindow = $true
        $start_info.RedirectStandardOutput = $true
        $start_info.RedirectStandardError = $true
        $start_info.RedirectStandardInput = $true
        $start_info.EnvironmentVariables['GIT_TERMINAL_PROMPT'] = '0'
        $start_info.EnvironmentVariables['GCM_INTERACTIVE'] = 'Never'
        $start_info.EnvironmentVariables['GIT_ASKPASS'] = ''
        $start_info.EnvironmentVariables['SSH_ASKPASS_REQUIRE'] = 'never'
        $start_info.EnvironmentVariables['SSH_ASKPASS'] = ''
        $git_process = New-Object System.Diagnostics.Process
        $git_process.StartInfo = $start_info
        if (-not $git_process.Start()) { throw 'Cannot start Git to verify the remote library revision.' }
        $git_process.StandardInput.Close()
        $stdout_task = $git_process.StandardOutput.ReadToEndAsync()
        $stderr_task = $git_process.StandardError.ReadToEndAsync()
        if (-not $git_process.WaitForExit(15000)) {
            $git_process.Kill()
            throw 'Remote library revision lookup timed out after 15 seconds.'
        }
        if (-not $stdout_task.Wait(1000) -or -not $stderr_task.Wait(1000)) {
            throw 'Cannot read the remote library revision response within the timeout.'
        }
        $stdout = $stdout_task.Result
        $stderr = $stderr_task.Result.Trim()
        if ($git_process.ExitCode -ne 0) {
            if (-not $stderr) { $stderr = 'Git could not query origin.' }
            throw $stderr
        }
        $remote_refs = @{}
        $preferred_branch = ''
        foreach ($line in ($stdout -split "\r?\n")) {
            if ($line -match '^ref:\s+refs/heads/(main|master)\s+HEAD$') {
                $preferred_branch = $Matches[1].ToLowerInvariant()
            }
            elseif ($line -match '^([a-fA-F0-9]{40})\s+refs/heads/(main|master)$') {
                $remote_refs[$Matches[2].ToLowerInvariant()] = $Matches[1].ToLowerInvariant()
            }
        }
        if ($preferred_branch -and $remote_refs.ContainsKey($preferred_branch)) {
            $remote_branch = $preferred_branch
        }
        elseif ($remote_refs.ContainsKey('main')) { $remote_branch = 'main' }
        elseif ($remote_refs.ContainsKey('master')) { $remote_branch = 'master' }
        else { throw 'The library origin has no main or master branch to use as the official reference.' }
        return [pscustomobject]@{ branch = $remote_branch; ref = $remote_refs[$remote_branch]; error = '' }
    }
    catch {
        return [pscustomobject]@{ branch = ''; ref = ''; error = $_.Exception.Message }
    }
    finally {
        if ($null -ne $git_process) { $git_process.Dispose() }
    }
}

# Ignore .vcxproj files entirely in the local prebuild Dirty decision.
# They remain tracked, visible and committable. Other firmware and library files
# retain normal Dirty checks, including staged and untracked changes.
function Get-BuildRelevantFirmwareStatus([string]$Repository) {
    return Invoke-Git $Repository @('status', '--porcelain', '--untracked-files=all',
        '--', '.', ':(exclude,glob)**/*.vcxproj')
}

# Protected branches always block Dirty builds. Use only plain ASCII text
# so Visual Micro, Arduino IDE build logs and CI display the same diagnostic.
# No dialog is shown and no Git checkout or working tree is changed.
function Stop-DirtyProtectedBuild([string]$Branch, [string[]]$Repositories) {
    $lines = @(
        "======================================================================",
        "GPM - DIRTY GIT REPOSITORIES  [BUILD BLOCKED]",
        "======================================================================",
        "",
        "ERROR: Uncommitted changes detected on a protected firmware branch.",
        "",
        "FIRMWARE BRANCH",
        "  $Branch",
        "",
        "DIRTY REPOSITORIES"
    )
    foreach ($repositoryName in $Repositories) {
        $lines += "  - $repositoryName"
    }
    $lines += @(
        "",
        "REQUIRED ACTION - CLEAN REPOSITORIES BEFORE REBUILDING",
        "  1. CHECK THE FIRMWARE AND ALL EXTERNAL LIBRARY REPOSITORIES.",
        "  2. COMMIT, STASH OR DISCARD UNCOMMITTED CHANGES AS APPROPRIATE.",
        "  3. VERIFY THE REPOSITORIES ARE CLEAN FOR BUILD, THEN REBUILD.",
        "",
        "NO FILES, GIT REVISIONS OR MANIFESTS WERE CHANGED AUTOMATICALLY.",
        "======================================================================",
        "BUILD BLOCKED"
    )
    throw ($lines -join [Environment]::NewLine)
}

# Release mismatch diagnostics remain read-only and blocking; the copy button
# never changes the Git checkout or manifest.
function Show-ReleaseLockMismatch([string]$Details) {
    Write-Warning $Details
    if ($env:CI -or $env:GITHUB_ACTIONS -or -not [Environment]::UserInteractive -or
        [Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw "GPM RELEASE LOCK MISMATCH: $Details"
    }
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $dialog = New-Object System.Windows.Forms.Form
    $dialog.Text = 'GPM - Git Revision Mismatch (Build Blocked)'
    $dialog.Size = New-Object System.Drawing.Size(840, 490)
    $dialog.StartPosition = 'CenterScreen'
    $dialog.FormBorderStyle = 'FixedDialog'
    $dialog.MaximizeBox = $false
    $dialog.MinimizeBox = $false
    $dialog.TopMost = $true
    $detailsText = New-Object System.Windows.Forms.TextBox
    $detailsText.Multiline = $true
    $detailsText.ReadOnly = $true
    $detailsText.WordWrap = $false
    $detailsText.ScrollBars = 'Both'
    $detailsText.Text = $Details
    $detailsText.SetBounds(14, 14, 795, 370)
    $dialog.Controls.Add($detailsText)
    $copyButton = New-Object System.Windows.Forms.Button
    $copyButton.Text = 'Copy all hashes'
    $copyButton.SetBounds(14, 405, 145, 30)
    $copyButton.Add_Click({ [System.Windows.Forms.Clipboard]::SetText($detailsText.Text) })
    $dialog.Controls.Add($copyButton)
    $closeButton = New-Object System.Windows.Forms.Button
    $closeButton.Text = 'Close - cancel build'
    $closeButton.SetBounds(640, 405, 168, 30)
    $closeButton.Add_Click({ $dialog.Close() })
    $dialog.Controls.Add($closeButton)
    [void]$dialog.ShowDialog()
    $dialog.Dispose()
    throw 'GPM RELEASE LOCK MISMATCH: build blocked; restore the expected commits manually.'
}

function Verify-ReleaseLock([string]$Repository, [string]$Branch,
        [string]$FirmwareHead, [string]$ManifestPath, [object[]]$Revisions) {
    if ($Branch -notmatch '^(release/.+|main|master)$') { return }
    # A local-only release can be rebuilt before its first push. Never infer
    # unpublished status from a failed remote lookup or a missing audit snapshot.
    $local_only_release = $false
    $oldPrompt = $env:GIT_TERMINAL_PROMPT
    $env:GIT_TERMINAL_PROMPT = '0'
    try {
        if ($Branch -match '^release/.+$') {
            # Query live origin refs, not potentially stale local tracking refs.
            $remote_refs = & git -C $Repository ls-remote --heads origin "refs/heads/$Branch" 'refs/heads/gpm-release-locks' 2>&1
            if ($LASTEXITCODE -ne 0) {
                Show-ReleaseLockMismatch "Cannot determine publication state for $Branch; remote Git lookup failed. Build blocked."
            }
            $remote_names = @($remote_refs | ForEach-Object { ([string]$_ -split '\s+')[-1] })
            $local_only_release = -not ($remote_names -contains "refs/heads/$Branch")
            if (-not ($remote_names -contains 'refs/heads/gpm-release-locks')) {
                if ($local_only_release) {
                    Write-Host "GPM local-only release: $Branch; remote lock not yet required."
                    return
                }
                Show-ReleaseLockMismatch "Missing release audit branch for published $Branch."
            }
        }

        # The audit branch lives separately from firmware and never changes
        # the pinned firmware SHA. Published releases always require it.
        $fetched = & git -C $Repository fetch --quiet --no-tags origin 'refs/heads/gpm-release-locks:refs/remotes/origin/gpm-release-locks' 2>&1
        if ($LASTEXITCODE -ne 0) {
            Show-ReleaseLockMismatch "Missing or unreachable GPM release revision audit branch. Cannot read origin/gpm-release-locks for $Branch. Complete the GitHub Actions revision recorder, then retry. Git details: $($fetched -join ' ')"
        }
    }
    catch {
        throw "Cannot verify release lock. $($_.Exception.Message) Check GitHub Actions revision recording and network access."
    }
    finally { $env:GIT_TERMINAL_PROMPT = $oldPrompt }

    $key = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Branch)).TrimEnd('=').Replace('+','-').Replace('/','_')
    $auditPath = ".gpm/release-locks/$key.json"
    # Check tree paths before reading a blob: absent paths must not produce a
    # raw fatal Git error under PowerShell's ErrorActionPreference=Stop.
    $audit_paths = & git -C $Repository ls-tree -r --name-only refs/remotes/origin/gpm-release-locks -- $auditPath
    if ($LASTEXITCODE -ne 0) {
        Show-ReleaseLockMismatch "Cannot inspect remote release audit tree for $Branch."
    }
    if (-not (@($audit_paths) -ccontains $auditPath)) {
        if ($local_only_release) {
            Write-Host "GPM local-only release: $Branch; no published revision snapshot."
            return
        }
        Show-ReleaseLockMismatch "Missing snapshot for $Branch in origin/gpm-release-locks:$auditPath. Let the post-merge GitHub Actions job finish (or dispatch it on this branch) before rebuilding."
    }
    if ($local_only_release) {
        Show-ReleaseLockMismatch "A revision snapshot exists for $Branch, but its remote branch is missing. Cannot treat it as a new local release."
    }
    $snapshot = & git -C $Repository show "refs/remotes/origin/gpm-release-locks:$auditPath" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $snapshot) {
        Show-ReleaseLockMismatch "Cannot read revision snapshot for $Branch in origin/gpm-release-locks."
    }
    try { $lock = ($snapshot -join [Environment]::NewLine) | ConvertFrom-Json }
    catch { Show-ReleaseLockMismatch "Invalid revision lock JSON in origin/gpm-release-locks:$auditPath." }
    if ($lock.schemaVersion -ne 1 -or $lock.firmwareBranch -cne $Branch) {
        Show-ReleaseLockMismatch "Invalid schema/branch in revision lock for $Branch."
    }
    $digest = Invoke-Git $Repository @('rev-parse', 'HEAD:.gpm/avr-libraries.json')

    $errors = @()
    if ($lock.firmwareCommit -cne $FirmwareHead) {
        $errors += "FIRMWARE mismatch: expected $($lock.firmwareCommit), actual $FirmwareHead"
    }
    if ($lock.manifestBlob -cne $digest) {
        $errors += "LIBRARY manifest mismatch: expected Git blob $($lock.manifestBlob), actual $digest"
    }
    foreach ($revision in $Revisions) {
        if ([string]$revision.source.ref -cne [string]$revision.head) {
            $errors += "LIBRARY $($revision.source.url): expected $($revision.source.ref), actual $($revision.head)"
        }
    }
    if ($errors.Count -gt 0) {
        # ASCII-only diagnostics work in Visual Micro, IDE build logs and CI.
        # Keep the expected/actual hashes visible and do not change release checks.
        $lines = @(
            "======================================================================",
            "GPM - GIT REVISION MISMATCH  [BUILD BLOCKED]",
            "======================================================================",
            "",
            "ERROR: Current Git revisions do not match this release snapshot.",
            "",
            "RELEASE",
            "  FIRMWARE BRANCH: $Branch",
            "",
            "FIRMWARE COMMIT",
            "  EXPECTED: $($lock.firmwareCommit)",
            "  ACTUAL:   $FirmwareHead",
            "",
            "LIBRARY MANIFEST (GIT BLOB)",
            "  EXPECTED: $($lock.manifestBlob)",
            "  ACTUAL:   $digest",
            "",
            "EXTERNAL GIT LIBRARIES"
        )
        foreach ($pinned in @($lock.gitLibraries | Where-Object { $null -ne $_ })) {
            $found = @($Revisions | Where-Object { $_.source.url -eq $pinned.url })
            $actual = if ($found.Count -eq 1) { $found[0].head } else { 'not linked' }
            $lines += @(
                "  REPOSITORY: $($pinned.url)",
                "    EXPECTED: $($pinned.sha)",
                "    ACTUAL:   $actual"
            )
        }
        $lines += @("", "MISMATCH DETAILS")
        foreach ($detail in $errors) { $lines += "  - $detail" }
        $lines += @(
            "",
            "REQUIRED ACTION - VERIFY BEFORE REBUILDING",
            "  1. CHECK THE FIRMWARE GIT COMMIT HASH.",
            "  2. CHECK ALL EXTERNAL LIBRARY GIT COMMIT HASHES.",
            "  3. CHECK THE LIBRARY MANIFEST AGAINST THE RELEASE SNAPSHOT.",
            "  4. RESTORE THE REQUIRED GIT REVISIONS MANUALLY, THEN REBUILD.",
            "",
            "NO GIT CHECKOUT OR MANIFEST UPDATE WAS PERFORMED AUTOMATICALLY.",
            "======================================================================",
            "BUILD BLOCKED"
        )
        Show-ReleaseLockMismatch ($lines -join [Environment]::NewLine)
    }
    Write-Host "GPM revision lock verified: $Branch @ $FirmwareHead"
}
function Normalize-Remote([string]$Remote) {
    $url = $Remote.Trim()
    if ($url -match '^git@github\.com:(.+)$') { $url = "https://github.com/$($Matches[1])" }
    elseif ($url -match '^ssh://git@github\.com/(.+)$') { $url = "https://github.com/$($Matches[1])" }
    return ($url -replace '\.git$', '').TrimEnd('/')
}
# Inspecting a hash in a Git client may also change the local checkout. Never
# publish decisions made against an earlier HEAD, branch or worktree state.
function assert_repository_unchanged([string]$repository_root, [string]$expected_head, [string]$expected_branch, [string]$expected_status) {
    $current_head = Invoke-Git $repository_root @('rev-parse', 'HEAD')
    $current_branch = Invoke-Git $repository_root @('branch', '--show-current')
    $current_status = Invoke-Git $repository_root @('status', '--porcelain', '--untracked-files=all')
    if ($current_head -cne $expected_head -or $current_branch -cne $expected_branch -or $current_status -cne $expected_status) {
        throw "Repository changed during prebuild verification: $repository_root. Rebuild to verify the new revision and working tree."
    }
}
try {
    $repo = Invoke-Git $SketchPath @('rev-parse', '--show-toplevel')
    $gitDir = Invoke-Git $repo @('rev-parse', '--absolute-git-dir')
    $branch = Invoke-Git $repo @('branch', '--show-current')
    $firmware_head = Invoke-Git $repo @('rev-parse', 'HEAD')
    $manifestFile = Join-Path $repo '.gpm/avr-libraries.json'
    if (-not (Test-Path -LiteralPath $manifestFile)) {
        throw "Missing AVR dependency manifest: $manifestFile"
    }
    $manifest_snapshot = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8
    $manifest = $manifest_snapshot | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1) { throw 'Unsupported AVR dependency manifest schema' }
    $repositories = @($manifest.gitRepositories | Where-Object { $null -ne $_ })
    $mappingFile = Join-Path $gitDir 'gpm-avr-library-paths.json'
    if ($repositories.Count -gt 0 -and -not (Test-Path -LiteralPath $mappingFile)) {
        throw $localLibraryLinkError
    }
    $localSources = @()
    if (Test-Path -LiteralPath $mappingFile) {
        $mapping = Get-Content -LiteralPath $mappingFile -Raw -Encoding UTF8 | ConvertFrom-Json
        $localSources = @($mapping.sources | Where-Object { $null -ne $_ })
    }
    $developmentBranch = $branch -match '^(develop|feature/.+|fix/.+|bugfix/.+|task/.+|hotfix/.+)$'
    $hasDirtyLibraries = $false
    $hasUnofficialLibraryBranch = $false
    $dirtyNames = New-Object 'System.Collections.Generic.List[string]'
    # Keep the full working-tree snapshot for concurrent-change detection.
    $firmwareStatus = Invoke-Git $repo @('status', '--porcelain', '--untracked-files=all')
    $buildRelevantStatus = Get-BuildRelevantFirmwareStatus $repo
    if ($buildRelevantStatus) {
        $dirtyNames.Add((Split-Path $repo -Leaf))
    }
    elseif ($firmwareStatus) {
        Write-Host 'GPM: .vcxproj changes excluded from build Dirty check; files remain tracked by Git.'
    }
    $localRevisions = @()
    $revisionErrors = @()
    $revision_checks = @()
    foreach ($source in $repositories) {
        if ([string]$source.ref -notmatch '^[a-fA-F0-9]{40}$') { throw "Invalid manifest Git SHA for $($source.url)" }
        $candidates = @($localSources | Where-Object {
            (Normalize-Remote $_.url) -ieq (Normalize-Remote $source.url)
        })
        if ($candidates.Count -ne 1) {
            throw $localLibraryLinkError
        }
        try { $libraryRoot = [System.IO.Path]::GetFullPath($candidates[0].path) }
        catch { throw $localLibraryLinkError }
        if (-not (Test-Path -LiteralPath $libraryRoot -PathType Container)) {
            throw $localLibraryLinkError
        }
        try { $actualRoot = Invoke-Git $libraryRoot @('rev-parse', '--show-toplevel') }
        catch {
            if (-not (Test-Path -LiteralPath (Join-Path $libraryRoot '.git'))) { throw $localLibraryLinkError }
            throw
        }
        if ([System.IO.Path]::GetFullPath($actualRoot) -ine $libraryRoot) {
            throw $localLibraryLinkError
        }
        $origin = Invoke-Git $libraryRoot @('remote', 'get-url', 'origin')
        if ((Normalize-Remote $origin) -ine (Normalize-Remote $source.url)) {
            throw "Wrong Git remote for $libraryRoot ($origin, expected $($source.url))"
        }
        $status = Invoke-Git $libraryRoot @('status', '--porcelain', '--untracked-files=all')
        if ($status) {
            $dirtyNames.Add((Split-Path $libraryRoot -Leaf))
            $hasDirtyLibraries = $true
        }
        $head = Invoke-Git $libraryRoot @('rev-parse', 'HEAD')
        if ($head -notmatch '^[a-fA-F0-9]{40}$') { throw "Invalid local Git SHA for $libraryRoot" }
        # This library's branch must be independent from the firmware branch.
        $libraryBranch = Invoke-Git $libraryRoot @('branch', '--show-current')
        if ($libraryBranch) {
            if ($libraryBranch -notin @('main', 'master')) { $hasUnofficialLibraryBranch = $true }
        }
        else {
            # Detached HEAD: the pinned commit is official if reachable from
            # local or remote main/master, without requiring a network fetch.
            $containing = Invoke-Git $libraryRoot @('branch', '--all', '--contains', 'HEAD', '--format=%(refname:short)')
            $officialBranches = @($containing -split "\r?\n" | Where-Object { $_ -in @('main', 'master', 'origin/main', 'origin/master') })
            if ($officialBranches.Count -eq 0) { $hasUnofficialLibraryBranch = $true }
        }
        $localRevisions += [pscustomobject]@{ url = $source.url; ref = $head }
        $revision_checks += [pscustomobject]@{ source = $source; head = $head; library_root = $libraryRoot; branch = $libraryBranch; status = $status }
        if ($source.ref -ne $head) {
            if (-not $developmentBranch) {
                $revisionErrors += @(
                    "======================================================================",
                    "GPM - GIT REVISION MISMATCH  [BUILD BLOCKED]",
                    "======================================================================",
                    "ERROR: Library Git commit does not match the pinned manifest.",
                    "  FIRMWARE BRANCH: $branch",
                    "  LIBRARY:         $($source.url)",
                    "  EXPECTED COMMIT: $($source.ref)",
                    "  ACTUAL COMMIT:   $head",
                    "",
                    "REQUIRED ACTION - VERIFY BEFORE REBUILDING",
                    "  1. CHECK THE FIRMWARE GIT COMMIT HASH.",
                    "  2. CHECK ALL EXTERNAL LIBRARY GIT COMMIT HASHES.",
                    "  3. CHECK THE LIBRARY MANIFEST FOR THIS RELEASE.",
                    "  4. RESTORE THE PINNED LIBRARY COMMIT MANUALLY, OR UPDATE THE",
                    "     MANIFEST THROUGH FEATURE/DEVELOP -> RELEASE -> MAIN.",
                    "",
                    "BUILD BLOCKED",
                    "======================================================================"
                ) -join [Environment]::NewLine
            }
            else {
                Write-Warning "Development uses local library commit $head (pinned manifest: $($source.ref)). Rebuild is read-only: run GPM on the application feature to record the chosen revisions before its PR."
            }
        }
        else { Write-Host "AVR library commit matches manifest: $head" }
    }

    # Never prompt on develop/feature: report dirty worktrees and preserve X.
    # Do not alter the manifest on any branch, even if libraries are clean.
    if ($dirtyNames.Count -gt 0) {
        $names = ($dirtyNames | Select-Object -Unique) -join ', '
        if (-not $developmentBranch) {
            if ($branch -match '^(release/.+|main|master)$') {
                Stop-DirtyProtectedBuild -Branch $branch -Repositories @($dirtyNames | Select-Object -Unique)
            }
            throw "Build blocked on protected branch '$branch': dirty repositories: $names"
        }
        Write-Warning "Uncommitted firmware/library changes: $names. Build will be marked X."
    }

    # Both official firmware and pinned library revisions are checked against
    # their post-merge snapshot. A mismatch invokes the single blocking form.
    Verify-ReleaseLock -Repository $repo -Branch $branch -FirmwareHead $firmware_head -ManifestPath $manifestFile -Revisions $revision_checks

    # A newer official library HEAD is never a reason to rewrite the manifest.
    # The branch-specific lock and committed dependency pin are authoritative.
    if ($revisionErrors.Count -gt 0) {
        throw ($revisionErrors -join [Environment]::NewLine)
    }
    # Recheck every snapshot before changing the manifest. Any concurrent checkout,
    # commit, working-tree change or manifest edit requires a new Rebuild.
    assert_repository_unchanged $repo $firmware_head $branch $firmwareStatus
    foreach ($revision_check in $revision_checks) {
        assert_repository_unchanged $revision_check.library_root $revision_check.head $revision_check.branch $revision_check.status
    }
    if (-not (Test-Path -LiteralPath $manifestFile) -or (Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8) -cne $manifest_snapshot) {
        throw 'AVR dependency manifest changed during prebuild verification. Rebuild to verify the new manifest.'
    }
    # The AVR prebuild is strictly read-only for tracked project files on all
    # branches. GPM captures selected library revisions on application features,
    # then the normal PR process carries the manifest to develop/release/main.
    # The generated header is ignored by Git. Its paired version references
    # are reproducible from the committed firmware and library manifest.
    $versions = @()
    foreach ($revision in $localRevisions) { $versions += "$($revision.url)@$($revision.ref)" }
    foreach ($lib in @($manifest.arduinoLibraryManager | Where-Object { $null -ne $_ })) {
        if ([string]::IsNullOrWhiteSpace($lib.version)) { throw "Unpinned Arduino Library Manager version: $($lib.name)" }
        $versions += "$($lib.name)@$($lib.version)"
    }
    # Same seven-character interface as GIT_VERSION, including on development branches.
    # One Git repository: its abbreviated commit. Multiple dependencies: a
    # deterministic seven-character fingerprint of all pinned references.
    # Always use a real Git SHA when exactly one Git repository is pinned.
    # Multiple dependencies (or none) use a deterministic SHA256 fingerprint;
    # an empty dependency set has the SHA256(empty string) fingerprint.
    if ($versions.Count -eq 1 -and $repositories.Count -eq 1) {
        $shortVersion = ([string]$localRevisions[0].ref).Substring(0, 7).ToLowerInvariant()
    }
    else {
        $data = [System.Text.Encoding]::UTF8.GetBytes(($versions -join ';'))
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try { $digest = $sha256.ComputeHash($data) }
        finally { $sha256.Dispose() }
        $shortVersion = [System.BitConverter]::ToString($digest).Replace('-', '').Substring(0, 7).ToLowerInvariant()
    }
    # Library origin is determined by library checkout branch(es), not firmware.
    # A combined version is unofficial if any Git library is unofficial.
    if ($hasUnofficialLibraryBranch) { $shortVersion += '*' }
    if ($hasDirtyLibraries) { $shortVersion += 'X' }
    $headerLine = "#define GIT_LIBRARIES_VERSION `"$shortVersion`"`n"
    [System.IO.File]::AppendAllText($GeneratedHeader, $headerLine, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "GIT_LIBRARIES_VERSION: $shortVersion"
    Write-Host "AVR library revision(s): $($versions -join '; ')"
    exit 0
}
catch {
    if ($_.Exception.Message -eq $localLibraryLinkError) {
        [Console]::Error.WriteLine("ERROR: AVR local libraries not linked or path invalid.")
        [Console]::Error.WriteLine("Run Git Project Manager (GPM) to configure library paths, then rebuild.")
    }
    else {
        [Console]::Error.WriteLine("GPM AVR library version error: $($_.Exception.Message)")
    }
    exit 1
}

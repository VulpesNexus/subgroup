<#
.SYNOPSIS
    Where the About window opens, measured without Illustrator.

.DESCRIPTION
    The About box centers on Illustrator's window and is then pushed back
    inside the work area of whatever monitor that lands on. Until 1.0.4 it did
    only the first half, so an Illustrator against a screen edge could put the
    box half off the desktop or under the taskbar, where the prose in it cannot
    be read and cannot be scrolled to.

    None of this needs Illustrator. Where the window opens is decided entirely
    by the owner window's rectangle and the monitor that rectangle falls on, so
    tools\AboutHarness stands a plain window in for Illustrator, puts it where
    the interesting cases are -- against each edge, off each edge, and on a
    monitor with a negative origin -- and reports where the dialog landed.

    Each case is also scored against the placement the old code would have
    produced, from the same numbers. A test that passes equally before and
    after a fix has not tested the fix, so the rows say, case by case, whether
    the old arithmetic would have put the window somewhere unreachable.

    The harness is built here rather than committed, into a directory whose
    path is plain ASCII: this repository can live under a path containing
    characters the C++ toolchain's own console cannot represent, and cmd.exe
    cannot even change directory into one.

    This opens real windows on the real desktop for a few seconds. The stand-in
    window does not take focus, but the dialog is modal and will. Do not run it
    while someone is working at this machine.

.PARAMETER KeepHarness
    Leave the staged build directory in place afterward, to look at the
    harness or run it by hand.
#>
[CmdletBinding()]
param(
    [string] $LogPath,
    [switch] $KeepHarness
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $LogPath) { $LogPath = Join-Path $repo 'docs\about-placement.tsv' }

# This repository has no probe framework of its own, so the three lines of one
# it needs live here. The columns match the other plugins' evidence files.
$rows = New-Object Collections.Generic.List[string]
$rows.Add("probe`tgroup`tcase`texpected`tobserved`tstatus")
function Add-Row {
    param([string] $Group, [string] $Case, [string] $Expected, [string] $Observed, [string] $Status)
    $clean = { param($s) ($s -replace "[`t`r`n]+", ' ').Trim() }
    $rows.Add(("about-placement`t{0}`t{1}`t{2}`t{3}`t{4}" -f `
        (& $clean $Group), (& $clean $Case), (& $clean $Expected), (& $clean $Observed), $Status))
}

# ---------------------------------------------------------------- the harness

function Import-VisualStudioEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio 2022 or the Build Tools.' }
    $install = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
               Select-Object -First 1
    if (-not $install) { throw 'No Visual Studio installation with the C++ tools was found.' }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    # cmd cannot hold the repository path, so it is never given it: this runs
    # vcvars in its own shell purely to read the environment back out.
    $dumped = cmd /c "call `"$vcvars`" > nul 2>&1 && set"
    if (-not $dumped) { throw 'vcvars64.bat produced no environment.' }
    foreach ($line in $dumped) {
        if ($line -match '^([^=]+)=(.*)$' -and $matches[1] -notmatch '^(PROMPT|TERM)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

function Build-AboutHarness {
    param([string] $StageRoot)

    if (Test-Path $StageRoot) { [System.IO.Directory]::Delete($StageRoot, $true) }
    $src = Join-Path $StageRoot 'Source'
    $res = Join-Path $StageRoot 'Res'
    $bin = Join-Path $StageRoot 'H'
    $null = New-Item -ItemType Directory -Force -Path $src, $res, $bin

    Copy-Item (Join-Path $repo 'plugin\Source\*')        $src -Recurse -Force
    Copy-Item (Join-Path $repo 'plugin\Resources\Win\*') $res -Recurse -Force
    Copy-Item (Join-Path $repo 'tools\AboutHarness\*')   $bin -Recurse -Force

    Push-Location $bin
    try {
        # The compilers' own chatter is captured rather than left to fall out
        # of the function: anything a function writes is part of what it
        # returns, and a build log prepended to the path makes the caller's
        # $exe an array of two hundred strings.
        $out = & rc.exe -nologo -I $res -I $src -fo harness.res harness.rc 2>&1
        if ($LASTEXITCODE -ne 0) { $out | Write-Output; throw "rc.exe failed with exit code $LASTEXITCODE." }

        $out = & cl.exe -nologo -EHsc -W4 -std:c++17 -DUNICODE -D_UNICODE `
                 -I $src -I $res `
                 main.cpp (Join-Path $src 'SubgroupAbout.cpp') harness.res `
                 -Fe:AboutHarness.exe `
                 -link -SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib 2>&1
        if ($LASTEXITCODE -ne 0) { $out | Write-Output; throw "cl.exe failed with exit code $LASTEXITCODE." }
    }
    finally { Pop-Location }

    return (Join-Path $bin 'AboutHarness.exe')
}

# ------------------------------------------------------------------ the cases

Add-Type -AssemblyName System.Windows.Forms

$ownerW = 900
$ownerH = 700

function New-Case {
    param([string] $Name, [string] $Group, $Screen, [int] $X, [int] $Y)
    [pscustomobject]@{ Name = $Name; Group = $Group; X = $X; Y = $Y; Screen = $Screen }
}

$cases = New-Object Collections.Generic.List[object]

$primary = [System.Windows.Forms.Screen]::PrimaryScreen
$pw = $primary.WorkingArea

$cases.Add((New-Case 'no host window at all' 'ordinary' $null 0 0))

$cases.Add((New-Case 'Illustrator in the middle of the primary monitor' 'ordinary' $primary `
    ([int]($pw.Left + ($pw.Width - $ownerW) / 2)) ([int]($pw.Top + ($pw.Height - $ownerH) / 2))))

$cases.Add((New-Case 'Illustrator mostly off the left edge' 'against an edge' $primary `
    ([int]($pw.Left - $ownerW + 100)) ([int]($pw.Top + 200))))

$cases.Add((New-Case 'Illustrator mostly off the right edge' 'against an edge' $primary `
    ([int]($pw.Right - 100)) ([int]($pw.Top + 200))))

$cases.Add((New-Case 'Illustrator mostly off the top edge' 'against an edge' $primary `
    ([int]($pw.Left + 200)) ([int]($pw.Top - $ownerH + 100))))

$cases.Add((New-Case 'Illustrator mostly off the bottom edge, over the taskbar' 'against an edge' $primary `
    ([int]($pw.Left + 200)) ([int]($pw.Bottom - 100))))

$cases.Add((New-Case "Illustrator's center 60 px from the left edge" 'against an edge' $primary `
    ([int]($pw.Left + 60 - $ownerW / 2)) ([int]($pw.Top + 200))))

foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
    if ($screen.Primary) { continue }
    $w = $screen.WorkingArea
    $label = if ($w.Top -lt 0 -or $w.Left -lt 0) { 'a monitor with a negative origin' } else { 'a second monitor' }
    $cases.Add((New-Case "Illustrator in the middle of $($screen.DeviceName)" $label $screen `
        ([int]($w.Left + ($w.Width - $ownerW) / 2)) ([int]($w.Top + ($w.Height - $ownerH) / 2))))
    $cases.Add((New-Case "Illustrator against the bottom-right of $($screen.DeviceName)" $label $screen `
        ([int]($w.Right - 100)) ([int]($w.Bottom - 100))))
}

# ------------------------------------------------------------------- the runs

$stage = Join-Path $env:TEMP 'subgroup-about-harness'
$report = Join-Path $env:TEMP 'subgroup-about-placement.tsv'
if (Test-Path $report) { [System.IO.File]::Delete($report) }

Write-Output 'Building the About harness...'
Import-VisualStudioEnvironment
$exe = Build-AboutHarness -StageRoot $stage
Write-Output "Built $([System.IO.Path]::GetFileName($exe))"

function Invoke-Case {
    param($Case)
    $before = 0
    if (Test-Path $report) { $before = @(Get-Content $report).Count }

    $arguments = @("/owner:$($Case.X),$($Case.Y),$ownerW,$ownerH", "/report:$report", '/exit400')
    if ($null -eq $Case.Screen) { $arguments = @("/report:$report", '/exit400') }

    $run = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Normal
    if (-not $run.WaitForExit(30000)) {
        try { $run.Kill() } catch { }
        return $null
    }

    if (-not (Test-Path $report)) { return $null }
    $lines = @(Get-Content $report)
    if ($lines.Count -le $before) { return $null }

    $f = $lines[$lines.Count - 1] -split "`t"
    if ($f.Count -lt 13) { return $null }
    [pscustomobject]@{
        DlgL = [int]$f[0];  DlgT = [int]$f[1];  DlgR = [int]$f[2];  DlgB = [int]$f[3]
        OwnL = [int]$f[4];  OwnT = [int]$f[5];  OwnR = [int]$f[6];  OwnB = [int]$f[7]
        WrkL = [int]$f[8];  WrkT = [int]$f[9];  WrkR = [int]$f[10]; WrkB = [int]$f[11]
        Verdict = $f[12]
    }
}

$failures = 0

foreach ($case in $cases) {
    Write-Output "  $($case.Name)"
    $r = Invoke-Case -Case $case
    if ($null -eq $r) {
        Add-Row -Group $case.Group -Case $case.Name `
            -Expected 'the dialog opens wholly inside the work area' `
            -Observed 'the harness did not report a placement' -Status 'ERROR'
        $failures++
        continue
    }

    $dw = $r.DlgR - $r.DlgL
    $dh = $r.DlgB - $r.DlgT
    $inside = $r.Verdict -eq 'INSIDE'
    if (-not $inside) { $failures++ }

    Add-Row -Group $case.Group -Case $case.Name `
        -Expected 'the dialog opens wholly inside the work area of its monitor' `
        -Observed ("dialog {0},{1} {2}x{3}; work area {4},{5} to {6},{7}; {8}" -f `
                   $r.DlgL, $r.DlgT, $dw, $dh, $r.WrkL, $r.WrkT, $r.WrkR, $r.WrkB, $r.Verdict) `
        -Status $(if ($inside) { 'PASS' } else { 'FAIL' })

    if ($null -ne $case.Screen) {
        # Where the code before 1.0.4 would have put it: centered on the owner,
        # with nothing keeping it on the desktop.
        # Truncated, not rounded, because that is what the C++ does: [int] in
        # PowerShell rounds to even, so an odd difference came out one pixel
        # from the real answer and every unclamped case reported that the clamp
        # had moved the window.
        $oldX = $r.OwnL + [int][Math]::Truncate((($r.OwnR - $r.OwnL) - $dw) / 2)
        $oldY = $r.OwnT + [int][Math]::Truncate((($r.OwnB - $r.OwnT) - $dh) / 2)
        $oldInside = ($oldX -ge $r.WrkL) -and ($oldY -ge $r.WrkT) -and
                     (($oldX + $dw) -le $r.WrkR) -and (($oldY + $dh) -le $r.WrkB)
        $moved = ($oldX -ne $r.DlgL) -or ($oldY -ne $r.DlgT)

        Add-Row -Group 'the placement before 1.0.4' -Case $case.Name `
            -Expected 'recorded to show which cases discriminate; not a threshold' `
            -Observed ("centering alone gives {0},{1}, which is {2} the work area{3}" -f `
                       $oldX, $oldY, $(if ($oldInside) { 'inside' } else { 'OUTSIDE' }), `
                       $(if ($moved) { '; the clamp moved the window' } else { '; the clamp changed nothing' })) `
            -Status 'MEASURED'

        if ($oldInside) {
            $dxOff = [Math]::Abs((($r.DlgL + $r.DlgR) / 2) - (($r.OwnL + $r.OwnR) / 2))
            $dyOff = [Math]::Abs((($r.DlgT + $r.DlgB) / 2) - (($r.OwnT + $r.OwnB) / 2))
            $centered = ($dxOff -le 1) -and ($dyOff -le 1)
            if (-not $centered) { $failures++ }
            # Formatted through the invariant culture: -f formats in the
            # machine's own, and on this one a half-pixel offset prints as
            # "0,5", so "off by {0},{1}" came out as "off by 0,5,0,5".
            # A center can land on a half pixel whenever the window's width or
            # height is odd.
            $offset = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture,
                                       'off by {0} px across and {1} px down', $dxOff, $dyOff)
            Add-Row -Group 'centered on the host' -Case $case.Name `
                -Expected 'with no clamping needed, the dialog center sits on the host center' `
                -Observed $offset `
                -Status $(if ($centered) { 'PASS' } else { 'FAIL' })
        }
    }
}

$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath)
[System.IO.File]::WriteAllLines($LogPath, $rows)

Write-Output ""
Write-Output "Wrote $LogPath"
Write-Output $(if ($failures -eq 0) { 'about-placement: no failures' } else { "about-placement: $failures failed" })

if (-not $KeepHarness -and (Test-Path $stage)) {
    [System.IO.Directory]::Delete($stage, $true)
}

exit $(if ($failures -eq 0) { 0 } else { 1 })

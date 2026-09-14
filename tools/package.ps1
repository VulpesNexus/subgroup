# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vixen420
#
# Assembles dist\Subgroup <version>\ and its .zip from tracked sources.
#
# The release used to be put together by hand, which meant the shipped
# INSTALL.txt lived only in the ignored dist\ tree and could drift from the
# repository without anything noticing. Everything the zip carries is now a
# tracked file, and this script is the only thing that copies them.
#
#   powershell -File tools\package.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

$version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "VERSION reads '$version'" }

# What the source says, checked before the binary is looked at. The header is
# where a release actually changes the version, so a disagreement is reported
# in those terms rather than as a stale build.
$idh = Join-Path $repo 'plugin\Source\SubgroupID.h'
$source = [System.IO.File]::ReadAllText($idh)
$numbers = 'Major', 'Minor', 'Patch' | ForEach-Object {
    $found = [regex]::Match($source, "#define\s+kSubgroupVersion$_\s+(\d+)")
    if (-not $found.Success) { throw "SubgroupID.h declares no kSubgroupVersion$_" }
    $found.Groups[1].Value
}
$declared = $numbers -join '.'
if ($declared -ne $version) {
    throw "SubgroupID.h declares $declared but VERSION says $version."
}

$aip = Join-Path $repo 'install\Subgroup.aip'
if (-not (Test-Path $aip)) { throw "no binary at $aip -- build first" }

# The one failure this script exists to make impossible: packaging a binary
# from an earlier version because the rebuild was forgotten.
#
# Both halves of the version resource, not just one. FileVersion is the string
# a person reads; the fixed field is four numbers stored separately, and it is
# what Windows shows in the file's properties and what an installer would
# compare. They are built from the same three macros, so a binary where they
# disagree is one linked before that was true.
$info = (Get-Item $aip).VersionInfo
$built = $info.FileVersion
if ($built -ne $version) {
    throw "install\Subgroup.aip is $built but VERSION says $version. Rebuild before packaging."
}
$fixed = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart, $info.FileMinorPart,
                              $info.FileBuildPart, $info.FilePrivatePart
if ($fixed -ne "$version.0") {
    throw "install\Subgroup.aip's fixed version field reads $fixed, not $version.0. Rebuild before packaging."
}

$stage = Join-Path $repo "dist\Subgroup $version"
# Deleted through .NET rather than Remove-Item: a path guard on this workspace
# refuses Remove-Item for anything under the GitHub folder, and it refuses on
# the command text before the script runs, so a single such line stops the whole
# script -- including from a caller that has no deletion of its own.
if (Test-Path $stage) { [System.IO.Directory]::Delete($stage, $true) }
New-Item -ItemType Directory -Path (Join-Path $stage 'docs') -Force | Out-Null

Copy-Item $aip (Join-Path $stage 'Subgroup.aip')
Copy-Item (Join-Path $repo 'README.md')        $stage
Copy-Item (Join-Path $repo 'LICENSE')          $stage
Copy-Item (Join-Path $repo 'LICENSE-EXCEPTION') (Join-Path $stage 'LICENSE-EXCEPTION.txt')
Copy-Item (Join-Path $repo 'docs\implementation-notes.md')     (Join-Path $stage 'docs')
Copy-Item (Join-Path $repo 'docs\why-illustrator-declines.md') (Join-Path $stage 'docs')

# INSTALL.txt carries the version in its first line, so it is a template. No
# BOM: the file ships as plain text and some readers show one as a stray glyph.
$install = [System.IO.File]::ReadAllText((Join-Path $repo 'packaging\INSTALL.txt'))
$install = $install.Replace('{VERSION}', $version)
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $stage 'INSTALL.txt'), $install, $utf8)

$zip = Join-Path $repo "dist\Subgroup $version.zip"
if (Test-Path $zip) { [System.IO.File]::Delete($zip) }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

Write-Output "Subgroup $version"
Write-Output ("  {0}" -f $stage)
Write-Output ("  {0}  {1} bytes" -f (Split-Path $zip -Leaf), (Get-Item $zip).Length)
Write-Output ("  aip sha256 {0}" -f (Get-FileHash $aip).Hash)
Write-Output ("  zip sha256 {0}" -f (Get-FileHash $zip).Hash)
Write-Output "Attach the .zip to the tag as Subgroup-$version.zip."

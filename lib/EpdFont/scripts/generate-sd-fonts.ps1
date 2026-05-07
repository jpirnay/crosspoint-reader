#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Generate recommended SD card font packs for CrossPoint.
#
# Prerequisites:
#   pip install freetype-py fonttools
#
# Source fonts are in ..\builtinFonts\source\:
#   Bookerly\, NotoSans\, OpenDyslexic\, Ubuntu\ — committed to git
#   NotoSansCJK\ — downloaded automatically by this script (gitignored)
#
# Output goes to .\output\ (copy to SD card at \.crosspoint\fonts\)

Set-Location $PSScriptRoot

$script   = '.\fontconvert_sdcard.py'
$fontDir  = '..\builtinFonts\source'
$outputBase = '.\output'
$sizes    = '12,14,16,18'

# --- Download fonts that aren't checked into git ---

$notoSansCjkDir  = "$fontDir\NotoSansCJK"
$notoSansCjkFont = "$notoSansCjkDir\NotoSansCJKsc-Regular.otf"

if (-not (Test-Path $notoSansCjkFont)) {
    Write-Host 'Downloading NotoSansCJKsc-Regular.otf...'
    New-Item -ItemType Directory -Force -Path $notoSansCjkDir | Out-Null
    $url = 'https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf'
    Invoke-WebRequest -Uri $url -OutFile $notoSansCjkFont -UseBasicParsing
    $sizeMb = [math]::Round((Get-Item $notoSansCjkFont).Length / 1MB, 1)
    Write-Host "Downloaded ${sizeMb}MB to $notoSansCjkFont"
}

# Clean output directories to ensure a fresh build
Write-Host 'Cleaning output directories...'
Remove-Item -Recurse -Force "$outputBase\NotoSansExtended", "$outputBase\Bookerly-SD", "$outputBase\NotoSansCJK" -ErrorAction SilentlyContinue

Write-Host '=== Starting parallel font generation ==='

$jobs = @(
    @{
        Label = 'NotoSansExtended (Latin-ext + Greek + Cyrillic + Georgian + Armenian + Ethiopic)'
        Args  = @(
            $script,
            "$fontDir\NotoSans\NotoSans-Regular.ttf",
            '--intervals', 'latin-ext,greek,cyrillic,georgian,armenian,ethiopic',
            '--sizes', $sizes, '--style', 'regular',
            '--name', 'NotoSansExtended',
            '--output-dir', "$outputBase\NotoSansExtended\"
        )
    },
    @{
        Label = 'Bookerly-SD (multi-style)'
        Args  = @(
            $script,
            '--regular',    "$fontDir\Bookerly\Bookerly-Regular.ttf",
            '--bold',       "$fontDir\Bookerly\Bookerly-Bold.ttf",
            '--italic',     "$fontDir\Bookerly\Bookerly-Italic.ttf",
            '--bolditalic', "$fontDir\Bookerly\Bookerly-BoldItalic.ttf",
            '--intervals', 'builtin',
            '--sizes', $sizes, '--force-autohint',
            '--name', 'Bookerly-SD',
            '--output-dir', "$outputBase\Bookerly-SD\"
        )
    },
    @{
        Label = 'NotoSansCJK (CJK + ASCII + Punctuation)'
        Args  = @(
            $script,
            $notoSansCjkFont,
            '--intervals', 'ascii,latin1,punctuation,cjk',
            '--sizes', $sizes, '--style', 'regular',
            '--name', 'NotoSansCJK',
            '--output-dir', "$outputBase\NotoSansCJK\"
        )
    }
)

$runningJobs = @()
$i = 1
foreach ($job in $jobs) {
    Write-Host "[$i/$($jobs.Count)] $($job.Label)"
    $runningJobs += Start-Job -ScriptBlock {
        param($args)
        python @args
    } -ArgumentList (, $job.Args)
    $i++
}

$failed = $false
$i = 1
foreach ($job in $runningJobs) {
    $result = Wait-Job $job | Receive-Job
    if ($job.State -eq 'Failed') {
        Write-Host "ERROR: $($jobs[$i - 1].Label) generation failed"
        $failed = $true
    } else {
        $result | Write-Host
    }
    Remove-Job $job
    $i++
}

if ($failed) {
    Write-Host '=== Some font generations failed ==='
    exit 1
}

Write-Host ''
Write-Host '=== Done ==='
Write-Host "Copy the contents of $outputBase\ to your SD card at \.crosspoint\fonts\"

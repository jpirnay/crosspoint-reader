#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Set-Location $PSScriptRoot

$readerFontStyles = @('Regular', 'Italic', 'Bold', 'BoldItalic')
$berkelyFontSizes = @(10, 12, 14, 16, 18)
$notoSansFontSizes = @(10, 12, 14, 16, 18)

foreach ($size in $berkelyFontSizes) {
    foreach ($style in $readerFontStyles) {
        $fontName   = "bookerly_${size}_$($style.ToLower())"
        $fontPath   = "..\builtinFonts\source\Bookerly\Bookerly-${style}.ttf"
        $outputPath = "..\builtinFonts\${fontName}.h"
        python fontconvert.py $fontName $size $fontPath --2bit --compress | Set-Content $outputPath -Encoding UTF8
        Write-Host "Generated $outputPath"
    }
}

foreach ($size in $notoSansFontSizes) {
    foreach ($style in $readerFontStyles) {
        $fontName   = "notosans_${size}_$($style.ToLower())"
        $fontPath   = "..\builtinFonts\source\NotoSans\NotoSans-${style}.ttf"
        $outputPath = "..\builtinFonts\${fontName}.h"
        python fontconvert.py $fontName $size $fontPath --2bit --compress | Set-Content $outputPath -Encoding UTF8
        Write-Host "Generated $outputPath"
    }
}

$uiFontSizes  = @(10, 12)
$uiFontStyles = @('Regular', 'Bold')
$uiLangIntervals = @(
    '0x0000,0x007F'
    '0x0080,0x00FF'
    '0x0100,0x017F'
    '0x01A0,0x01A1'
    '0x01AF,0x01B0'
    '0x01C4,0x021F'
    '0x0300,0x036F'
    '0x0400,0x04FF'
    '0x1EA0,0x1EF9'
    '0x2010,0x206F'
    '0x20A0,0x20CF'
    '0xFB00,0xFB06'
    '0xFFFD,0xFFFD'
)

foreach ($size in $uiFontSizes) {
    foreach ($style in $uiFontStyles) {
        $fontName   = "inter_ui_${size}_$($style.ToLower())"
        $interPath  = "..\builtinFonts\source\Inter\Inter-${style}.ttf"
        $outputPath = "..\builtinFonts\${fontName}.h"

        $args = @('fontconvert.py', $fontName, $size, $interPath)
        foreach ($interval in $uiLangIntervals) {
            $args += '--additional-intervals'
            $args += $interval
        }
        python @args | Set-Content $outputPath -Encoding UTF8
        Write-Host "Generated $outputPath"
    }
}

python fontconvert.py notosans_8_regular 8 ..\builtinFonts\source\NotoSans\NotoSans-Regular.ttf |
    Set-Content ..\builtinFonts\notosans_8_regular.h -Encoding UTF8

Write-Host ''
Write-Host 'Running compression verification...'
python verify_compression.py ..\builtinFonts\

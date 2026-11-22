# Generates src/SoundStep.ico (an original "DSP" audio icon).
# Sizes: 16, 24, 32, 48, 64, 128, 256 (PNG-compressed ICO entries).
param(
    [string]$OutPath = (Join-Path $PSScriptRoot "..\src\SoundStep.ico")
)

Add-Type -AssemblyName System.Drawing

function New-RoundedRectPath([System.Drawing.RectangleF]$rect, [float]$radius) {
    $p = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = $radius * 2.0
    $p.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $p.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $p.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $p.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Get-HeavyFont([float]$size) {
    foreach ($name in @("Arial Black", "Segoe UI Black", "Arial")) {
        try {
            return [System.Drawing.Font]::new($name, $size, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        } catch {}
    }
    return [System.Drawing.Font]::new("Arial", $size, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
}

function Draw-Icon([int]$size) {
    $s = [double]$size
    $bmp = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    $m = [Math]::Max(1.0, $s * 0.045)
    $rectW = $s - 2.0 * $m
    $rect = [System.Drawing.RectangleF]::new([float]$m, [float]$m, [float]$rectW, [float]$rectW)
    $rad = [float]($s * 0.20)

    # Background: dark vertical gradient.
    $path = New-RoundedRectPath $rect $rad
    $cTop = [System.Drawing.Color]::FromArgb(255, 42, 42, 52)
    $cBot = [System.Drawing.Color]::FromArgb(255, 15, 15, 19)
    $bg = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $cTop, $cBot, 90.0)
    $g.FillPath($bg, $path)

    # Accent border.
    $borderW = [float][Math]::Max(1.0, $s * 0.022)
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 255, 150, 20), $borderW)
    $g.DrawPath($pen, $path)

    # Equalizer bars (audio/DSP motif).
    $bars = @(0.42, 0.82, 0.58, 0.72, 0.38)
    $barW = $s * 0.085
    $gap = $s * 0.022
    $totalW = 5.0 * $barW + 4.0 * $gap
    $startX = ($s - $totalW) / 2.0
    $baseY = $s * 0.70
    $maxH = $s * 0.40

    $barRect = [System.Drawing.RectangleF]::new(0.0, [float]($baseY - $maxH), [float]$s, [float]$maxH)
    $cBarA = [System.Drawing.Color]::FromArgb(255, 255, 150, 20)
    $cBarB = [System.Drawing.Color]::FromArgb(255, 255, 214, 80)
    $barBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($barRect, $cBarA, $cBarB, 90.0)

    for ($i = 0; $i -lt 5; $i++) {
        $h = [double]$bars[$i] * $maxH
        $x = $startX + $i * ($barW + $gap)
        $y = $baseY - $h
        $br = [System.Drawing.RectangleF]::new([float]$x, [float]$y, [float]$barW, [float]$h)
        $bp = New-RoundedRectPath $br ([float]($barW * 0.5))
        $g.FillPath($barBrush, $bp)
    }

    # "DSP" wording (only where legible).
    if ($size -ge 48) {
        $font = Get-HeavyFont ([float]($s * 0.22))
        $sf = [System.Drawing.StringFormat]::new()
        $sf.Alignment = [System.Drawing.StringAlignment]::Center
        $sf.LineAlignment = [System.Drawing.StringAlignment]::Center

        $trY = [float]($s * 0.62)
        $trH = [float]($s * 0.30)
        $textRect = [System.Drawing.RectangleF]::new(0.0, $trY, [float]$s, $trH)
        $shadowRect = [System.Drawing.RectangleF]::new(1.0, [float]($trY + 1.0), [float]$s, $trH)

        $shadow = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(120, 0, 0, 0))
        $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 245, 245, 250))
        $g.DrawString("DSP", $font, $shadow, $shadowRect, $sf)
        $g.DrawString("DSP", $font, $white, $textRect, $sf)
    }

    $g.Dispose()
    return $bmp
}

$sizes = @(256, 128, 64, 48, 32, 24, 16)
$entries = @()

foreach ($sz in $sizes) {
    $bmp = Draw-Icon $sz
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $data = $ms.ToArray()
    $entries += ,@($sz, $data)
    $bmp.Dispose()
    $ms.Dispose()
}

$count = $entries.Count
$offset = 6 + 16 * $count

$out = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($out)
$bw.Write([UInt16]0)          # reserved
$bw.Write([UInt16]1)          # type = icon
$bw.Write([UInt16]$count)     # count

$datas = @()
foreach ($e in $entries) {
    $sz = $e[0]
    $data = $e[1]
    $dim = if ($sz -ge 256) { 0 } else { $sz }
    $bw.Write([Byte]$dim)     # width
    $bw.Write([Byte]$dim)     # height
    $bw.Write([Byte]0)        # colors
    $bw.Write([Byte]0)        # reserved
    $bw.Write([UInt16]1)      # planes
    $bw.Write([UInt16]32)     # bit count
    $bw.Write([UInt32]$data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $data.Length
    $datas += ,$data
}
foreach ($d in $datas) { $bw.Write($d) }
$bw.Flush()

$full = [System.IO.Path]::GetFullPath($OutPath)
$dir = [System.IO.Path]::GetDirectoryName($full)
if (!(Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
[System.IO.File]::WriteAllBytes($full, $out.ToArray())
$out.Dispose()
$len = (Get-Item -LiteralPath $full).Length
Write-Output "Wrote $full ($len bytes)"

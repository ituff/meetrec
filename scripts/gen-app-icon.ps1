# MeetRec - generates src/App/MeetRec.ico (multi-size, PNG-compressed).
#
# The product mark is a blue rounded tile with a white camcorder glyph. Drawn
# directly at every size so the 16 px tray icon stays legible.
param(
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) "src\App\MeetRec.ico")
)

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @()

foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $blue = [System.Drawing.Color]::FromArgb(255, 0, 100, 234)
    $white = [System.Drawing.Color]::White
    $radius = [Math]::Max(3, [int]($size * 0.24))
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($size - $d - 1, 0, $d, $d, 270, 90)
    $path.AddArc($size - $d - 1, $size - $d - 1, $d, $d, 0, 90)
    $path.AddArc(0, $size - $d - 1, $d, $d, 90, 90)
    $path.CloseFigure()
    $brush = New-Object System.Drawing.SolidBrush($blue)
    $g.FillPath($brush, $path)

    $bodyX = [Math]::Round($size * 0.20)
    $bodyY = [Math]::Round($size * 0.32)
    $bodyW = [Math]::Round($size * 0.40)
    $bodyH = [Math]::Round($size * 0.34)
    $bodyR = [Math]::Max(1, [int]($bodyH * 0.28))
    $body = New-Object System.Drawing.Drawing2D.GraphicsPath
    $bd = $bodyR * 2
    $body.AddArc($bodyX, $bodyY, $bd, $bd, 180, 90)
    $body.AddArc($bodyX + $bodyW - $bd, $bodyY, $bd, $bd, 270, 90)
    $body.AddArc($bodyX + $bodyW - $bd, $bodyY + $bodyH - $bd, $bd, $bd, 0, 90)
    $body.AddArc($bodyX, $bodyY + $bodyH - $bd, $bd, $bd, 90, 90)
    $body.CloseFigure()
    $whiteBrush = New-Object System.Drawing.SolidBrush($white)
    $g.FillPath($whiteBrush, $body)

    $lens = @(
        (New-Object System.Drawing.PointF(($bodyX + $bodyW + $size * 0.02), ($bodyY + $bodyH * 0.25))),
        (New-Object System.Drawing.PointF(($size * 0.82), ($size * 0.49))),
        (New-Object System.Drawing.PointF(($bodyX + $bodyW + $size * 0.02), ($bodyY + $bodyH * 0.75)))
    )
    $g.FillPolygon($whiteBrush, $lens)

    $g.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($size, $stream.ToArray())
    $stream.Dispose()
    $bmp.Dispose()
    $brush.Dispose()
    $whiteBrush.Dispose()
}

$file = [System.IO.File]::Create($OutputPath)
$writer = New-Object System.IO.BinaryWriter($file)
$writer.Write([UInt16]0)             # reserved
$writer.Write([UInt16]1)             # type: icon
$writer.Write([UInt16]$images.Count) # count
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $size = $image[0]
    $bytes = $image[1]
    $dim = if ($size -ge 256) { 0 } else { $size }
    $writer.Write([Byte]$dim)
    $writer.Write([Byte]$dim)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)         # planes
    $writer.Write([UInt16]32)        # bpp
    $writer.Write([UInt32]$bytes.Length)
    $writer.Write([UInt32]$offset)
    $offset += $bytes.Length
}
foreach ($image in $images) {
    $writer.Write([Byte[]]$image[1])
}
$writer.Close()
$file.Close()

Write-Host "Wrote $OutputPath ($($images.Count) sizes)"

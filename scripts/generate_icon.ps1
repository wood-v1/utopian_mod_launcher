param(
    [Parameter(Mandatory = $true)]
    [string]$InputPng,

    [Parameter(Mandatory = $true)]
    [string]$OutputIco,

    [Parameter(Mandatory = $true)]
    [string]$OutputRc
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$source = [System.Drawing.Image]::FromFile($InputPng)
$frames = New-Object System.Collections.Generic.List[object]

try {
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap $size, $size
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.DrawImage($source, 0, 0, $size, $size)

            $stream = New-Object System.IO.MemoryStream
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $frames.Add([pscustomobject]@{
                    Size  = $size
                    Bytes = $stream.ToArray()
                }) > $null
            }
            finally {
                $stream.Dispose()
            }
        }
        finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
}
finally {
    $source.Dispose()
}

$icoDirectory = Split-Path -Parent $OutputIco
if ($icoDirectory) {
    [System.IO.Directory]::CreateDirectory($icoDirectory) > $null
}

$fileStream = [System.IO.File]::Open($OutputIco, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$writer = New-Object System.IO.BinaryWriter($fileStream)

try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$frames.Count)

    $offset = 6 + (16 * $frames.Count)
    foreach ($frame in $frames) {
        $dimension = if ($frame.Size -ge 256) { [byte]0 } else { [byte]$frame.Size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$frame.Bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $frame.Bytes.Length
    }

    foreach ($frame in $frames) {
        $writer.Write($frame.Bytes)
    }
}
finally {
    $writer.Dispose()
    $fileStream.Dispose()
}

$rcDirectory = Split-Path -Parent $OutputRc
if ($rcDirectory) {
    [System.IO.Directory]::CreateDirectory($rcDirectory) > $null
}

Set-Content -LiteralPath $OutputRc -Value 'IDI_APP_ICON ICON "uicon.ico"' -Encoding ASCII

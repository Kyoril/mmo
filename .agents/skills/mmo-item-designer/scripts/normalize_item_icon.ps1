param(
	[Parameter(Mandatory = $true)]
	[string]$InputPath,

	[Parameter(Mandatory = $true)]
	[string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path -LiteralPath $InputPath).Path
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($outputFullPath)
if (-not [System.IO.Directory]::Exists($outputDirectory))
{
	[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$source = [System.Drawing.Image]::FromFile($sourcePath)
try
{
	$cropSize = [Math]::Min($source.Width, $source.Height)
	$sourceX = [Math]::Floor(($source.Width - $cropSize) / 2)
	$sourceY = [Math]::Floor(($source.Height - $cropSize) / 2)

	$bitmap = New-Object System.Drawing.Bitmap 128, 128, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
	try
	{
		$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
		try
		{
			$graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
			$graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
			$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
			$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
			$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
			$graphics.DrawImage(
				$source,
				[System.Drawing.Rectangle]::new(0, 0, 128, 128),
				[System.Drawing.Rectangle]::new($sourceX, $sourceY, $cropSize, $cropSize),
				[System.Drawing.GraphicsUnit]::Pixel)
		}
		finally
		{
			$graphics.Dispose()
		}

		$bitmap.Save($outputFullPath, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally
	{
		$bitmap.Dispose()
	}
}
finally
{
	$source.Dispose()
}

$result = [System.Drawing.Image]::FromFile($outputFullPath)
try
{
	if ($result.Width -ne 128 -or $result.Height -ne 128)
	{
		throw "Output icon is $($result.Width)x$($result.Height), expected 128x128."
	}
}
finally
{
	$result.Dispose()
}

Write-Output "OK: $outputFullPath (128x128 PNG)"

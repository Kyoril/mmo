// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// Renders the DMG window background from the launcher's own art, so it follows the
// splash and the display font when they change.
//
//   swift make_background.swift <launcher res dir> <output.png> <scale>
//
// The window is 660 x 420 points; scale 2 renders the Retina version. build_dmg.sh
// combines both into one HiDPI TIFF. Icon positions must match dmg_settings.py.

import AppKit
import CoreText

let arguments = CommandLine.arguments
guard arguments.count == 4, let scale = Double(arguments[3]) else
{
	FileHandle.standardError.write("usage: make_background.swift <res dir> <out.png> <scale>\n".data(using: .utf8)!)
	exit(2)
}

let resources = URL(fileURLWithPath: arguments[1])
let output = URL(fileURLWithPath: arguments[2])

let width = 660.0
let height = 420.0
// Icon centers in points, from the top left, as in dmg_settings.py.
let leftIcon = CGPoint(x: 170, y: 205)
let rightIcon = CGPoint(x: 490, y: 205)

func fail(_ message: String) -> Never
{
	FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
	exit(1)
}

guard let splashSource = CGImageSourceCreateWithURL(resources.appendingPathComponent("splash.png") as CFURL, nil),
	let splash = CGImageSourceCreateImageAtIndex(splashSource, 0, nil) else
{
	fail("cannot read splash.png")
}

var fontError: Unmanaged<CFError>?
if !CTFontManagerRegisterFontsForURL(resources.appendingPathComponent("font_display.ttf") as CFURL, .process, &fontError)
{
	fail("cannot register font_display.ttf")
}
guard let descriptors = CTFontManagerCreateFontDescriptorsFromURL(resources.appendingPathComponent("font_display.ttf") as CFURL) as? [CTFontDescriptor],
	let displayDescriptor = descriptors.first else
{
	fail("font_display.ttf has no font")
}

let pixelWidth = Int(width * scale)
let pixelHeight = Int(height * scale)
guard let context = CGContext(data: nil, width: pixelWidth, height: pixelHeight, bitsPerComponent: 8, bytesPerRow: 0,
	space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else
{
	fail("cannot create the context")
}

// Work in points with a top-left origin, like the layout above.
context.scaleBy(x: scale, y: scale)
context.translateBy(x: 0, y: height)
context.scaleBy(x: 1, y: -1)

func color(_ r: Double, _ g: Double, _ b: Double, _ a: Double = 1) -> CGColor
{
	CGColor(srgbRed: r / 255, green: g / 255, blue: b / 255, alpha: a)
}

func drawImage(_ image: CGImage, in rect: CGRect)
{
	// CGContext.draw expects a bottom-left origin; flip locally.
	context.saveGState()
	context.translateBy(x: rect.minX, y: rect.maxY)
	context.scaleBy(x: 1, y: -1)
	context.draw(image, in: CGRect(origin: .zero, size: rect.size))
	context.restoreGState()
}

// The splash, scaled to cover the window around the castle (the launcher anchors it at 45 % height).
let splashAspect = Double(splash.width) / Double(splash.height)
var splashRect = CGRect(x: 0, y: 0, width: width, height: width / splashAspect)
if splashRect.height < height
{
	splashRect.size = CGSize(width: height * splashAspect, height: height)
}
splashRect.origin = CGPoint(x: (width - splashRect.width) / 2, y: (height - splashRect.height) * 0.45)
drawImage(splash, in: splashRect)

// Darken it towards the launcher's brown so text and icons read.
let scrim = CGGradient(colorsSpace: nil, colors: [color(20, 14, 8, 0.55), color(20, 14, 8, 0.82)] as CFArray, locations: [0, 1])!
context.drawLinearGradient(scrim, start: .zero, end: CGPoint(x: 0, y: height), options: [])

func drawText(_ text: String, font: CTFont, color textColor: CGColor, centerX: Double, baseline: Double, tracking: Double = 0)
{
	let attributes: [NSAttributedString.Key: Any] = [
		NSAttributedString.Key(kCTFontAttributeName as String): font,
		NSAttributedString.Key(kCTForegroundColorAttributeName as String): textColor,
		NSAttributedString.Key(kCTKernAttributeName as String): tracking,
	]
	let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: attributes))
	let lineWidth = CTLineGetTypographicBounds(line, nil, nil, nil)

	context.saveGState()
	context.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
	context.textPosition = CGPoint(x: centerX - lineWidth / 2, y: baseline)
	context.setShadow(offset: CGSize(width: 0, height: 1.5), blur: 4, color: color(0, 0, 0, 0.9))
	CTLineDraw(line, context)
	context.restoreGState()
}

let gold = color(232, 196, 120)
drawText("ALESTIA ONLINE", font: CTFontCreateWithFontDescriptor(displayDescriptor, 40, nil), color: gold, centerX: width / 2, baseline: 70, tracking: 1.5)

// Finder draws the icon labels itself, black in light mode and white in dark mode. A
// mid-tone bronze plaque behind them keeps both readable.
for center in [leftIcon, rightIcon]
{
	let plaque = CGRect(x: center.x - 92, y: center.y + 70, width: 184, height: 26)
	let path = CGPath(roundedRect: plaque, cornerWidth: 6, cornerHeight: 6, transform: nil)
	context.addPath(path)
	context.setFillColor(color(158, 132, 92, 0.92))
	context.fillPath()
	context.addPath(path)
	context.setStrokeColor(color(232, 196, 120, 0.8))
	context.setLineWidth(1)
	context.strokePath()
}

// An arrow from the game folder to Applications.
let arrowY = leftIcon.y
let arrowStart = leftIcon.x + 84
let arrowEnd = rightIcon.x - 84
context.saveGState()
context.setShadow(offset: CGSize(width: 0, height: 1.5), blur: 4, color: color(0, 0, 0, 0.9))
context.setStrokeColor(gold)
context.setFillColor(gold)
context.setLineWidth(5)
context.setLineCap(.round)
context.move(to: CGPoint(x: arrowStart, y: arrowY))
context.addLine(to: CGPoint(x: arrowEnd - 16, y: arrowY))
context.strokePath()
context.move(to: CGPoint(x: arrowEnd, y: arrowY))
context.addLine(to: CGPoint(x: arrowEnd - 22, y: arrowY - 14))
context.addLine(to: CGPoint(x: arrowEnd - 22, y: arrowY + 14))
context.closePath()
context.fillPath()
context.restoreGState()

let body = CTFontCreateUIFontForLanguage(.system, 14, nil)!
drawText("Drag Alestia Online to Applications to install.", font: body, color: color(240, 228, 205), centerX: width / 2, baseline: 352)
drawText("Then open the folder and start the Launcher; it downloads the game.", font: CTFontCreateUIFontForLanguage(.system, 12, nil)!,
	color: color(200, 186, 160), centerX: width / 2, baseline: 376)

guard let image = context.makeImage(),
	let destination = CGImageDestinationCreateWithURL(output as CFURL, "public.png" as CFString, 1, nil) else
{
	fail("cannot write \(output.path)")
}
// 72 dpi per point keeps the 2x image at the window's size in points.
CGImageDestinationAddImage(destination, image, [kCGImagePropertyDPIWidth: 72 * scale, kCGImagePropertyDPIHeight: 72 * scale] as CFDictionary)
if !CGImageDestinationFinalize(destination)
{
	fail("cannot write \(output.path)")
}

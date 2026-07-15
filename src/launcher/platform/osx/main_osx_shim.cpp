// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The macOS entry point has to live in an Objective-C++ translation unit, so this
// file only forwards to it.
//
// NOTE: macOS still runs the original Cocoa nib based UI in osx/. The reworked UI is
// Windows only for now; porting it means adding a shim next to platform/win32 that
// wraps the same composited buffer in a CGBitmapContext (kCGImageAlphaPremultipliedFirst
// | kCGBitmapByteOrder32Little is byte-for-byte the compositor's format) and
// implementing IPlatformHost plus LoadResourceBlob. No portable code has to change.

extern int main_osx(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	return main_osx(argc, argv);
}

// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "clipboard.h"

#include "base/macros.h"

#if PLATFORM_APPLE

#import <AppKit/AppKit.h>

namespace mmo
{
	namespace
	{
		/// The general pasteboard, holding UTF-8 text like the Windows clipboard implementation.
		class MacClipboard final : public IClipboard
		{
		public:
			bool SetText(const std::string_view text) override
			{
				@autoreleasepool
				{
					NSString* string = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
					if (!string)
					{
						return false;
					}

					NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
					[pasteboard clearContents];
					return [pasteboard setString:string forType:NSPasteboardTypeString];
				}
			}

			std::optional<std::string> GetText() override
			{
				@autoreleasepool
				{
					NSString* string = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
					if (!string)
					{
						return std::nullopt;
					}

					return std::string([string UTF8String]);
				}
			}
		};
	}

	std::unique_ptr<IClipboard> CreateClipboardImplementation()
	{
		return std::make_unique<MacClipboard>();
	}
}

#endif

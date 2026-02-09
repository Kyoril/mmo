// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "clipboard.h"

#include "base/macros.h"

#if !PLATFORM_WINDOWS


namespace mmo
{
	namespace
	{
		class NullClipboard final : public IClipboard
		{
		public:
			bool SetText(std::string_view) override
			{
				return false;
			}

			std::optional<std::string> GetText() override
			{
				return std::nullopt;
			}
		};
	}

	std::unique_ptr<IClipboard> CreateClipboardImplementation()
	{
		return std::make_unique<NullClipboard>();
	}
}

#endif


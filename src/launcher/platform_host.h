// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	/// Everything the portable UI needs from the operating system.
	///
	/// This is one of only three seams between the launcher's portable code and a
	/// platform: this interface, LoadResourceBlob, and handing the canvas a pixel
	/// buffer. Keeping it this small is what makes a second platform a new shim
	/// rather than a rewrite.
	class IPlatformHost
	{
	public:
		virtual ~IPlatformHost() = default;

		/// Starts the game and closes the launcher on success. Reports failure to the
		/// user itself.
		virtual void LaunchGame() = 0;

		virtual void Minimize() = 0;
		virtual void Close() = 0;

		/// A blocking, modal message. Used only for terminal conditions.
		virtual void ShowMessage(const std::string& title, const std::string& body) = 0;
	};
}

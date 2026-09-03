// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "startup_error.h"

#include "platform.h"

#include "graphics/graphics_device.h"
#include "graphics/render_window.h"
#include "log/default_log_levels.h"

#include <filesystem>

namespace mmo
{
	namespace
	{
		/// Resolves the config file path for display.
		/// @remarks The path is relative to the working directory, which is not necessarily the
		///          directory the executable sits in, so telling the player to look next to the
		///          game would be a guess. Resolve it and name the actual file.
		std::string GetDisplayConfigFilePath()
		{
			std::error_code error;
			const std::filesystem::path absolutePath = std::filesystem::absolute(ClientConfigFilePath, error);

			return error ? std::string(ClientConfigFilePath) : absolutePath.string();
		}

		/// Gets the render window out of the way so the error dialog is actually visible.
		/// @remarks Startup can fail after the graphics device was created, and on Windows the
		///          device may hold the display exclusively. A message box raised over an exclusive
		///          fullscreen surface can end up behind it, leaving the player with a black screen
		///          and a game that looks hung.
		void HideRenderWindow()
		{
			if (!GraphicsDevice::HasInstance())
			{
				return;
			}

			if (const RenderWindowPtr window = GraphicsDevice::Get().GetAutoCreatedWindow())
			{
				window->Hide();
			}
		}

		/// Builds the advice for a data path problem, which is the only kind of failure the config
		/// file can actually be blamed for.
		std::string GetDefaultAdvice()
		{
			return "The game cannot start without its data. If you did not move the game data on "
				"purpose, deleting this file resets the data path to its default and usually fixes "
				"the problem:\n\n" + GetDisplayConfigFilePath();
		}
	}

	/// @copydoc ShowStartupError
	void ShowStartupError(const std::string& details, const std::string& title, const std::string& advice)
	{
		ELOG(details);

		HideRenderWindow();

		Platform::ShowErrorDialog(title, details + "\n\n" + (advice.empty() ? GetDefaultAdvice() : advice));
	}
}

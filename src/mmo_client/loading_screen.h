// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
	namespace proto_client
	{
		class LoadingTips;
	}

	class LoadingScreen
	{
	public:
		static signal<void()> LoadingScreenShown;

	public:
		static void Init();

		static void Destroy();

		static void SetLoadingScreenTexture(const String& texture);

		/// @brief Sets the tips of which a random one is shown each time the loading screen appears.
		/// @param tips The tip table; must outlive the loading screen (or be replaced before it dies).
		static void SetTips(const proto_client::LoadingTips* tips);

		static void Paint();

		/// @brief Shows the loading screen. When it was hidden, the progress restarts at zero and a new tip is picked.
		static void Show();

		static void Hide();

		/// @brief Whether the loading screen is currently shown.
		static bool IsVisible();

		/// @brief Sets the loading progress shown by the progress bar.
		/// @param progress Progress in [0, 1]. The bar never moves backwards while the screen stays visible.
		static void SetProgress(float progress);

		/// @brief Draws and presents a frame showing only the loading screen right away.
		/// @details For loading code that blocks the event loop, so the progress bar keeps moving. Does
		///          nothing while hidden or inside a regular frame's paint, and is throttled unless @p force is set.
		static void Present(bool force = false);

	private:
		static void PaintContent();
	};

}

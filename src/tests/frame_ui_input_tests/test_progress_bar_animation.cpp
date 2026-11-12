// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "frame_ui/frame_mgr.h"
#include "frame_ui/localization.h"
#include "frame_ui/progress_bar.h"
#include "graphics/graphics_device.h"

#include "lua.hpp"

using namespace mmo;

namespace
{
	/// Owns a Lua state and a FrameManager for the duration of a test.
	struct ProgressBarFixture
	{
		ProgressBarFixture()
		{
			if (!GraphicsDevice::HasInstance())
			{
				GraphicsDevice::CreateNull(GraphicsDeviceDesc());
			}

			lua = luaL_newstate();
			luaL_openlibs(lua);
			luabind::open(lua);
			FrameManager::Initialize(lua, localization);

			bar = std::make_shared<ProgressBar>("ProgressBar", "TestProgressBar");
		}

		~ProgressBarFixture()
		{
			bar.reset();
			FrameManager::Get().ResetTopFrame();
			FrameManager::Destroy();
			lua_close(lua);
		}

		/// Advances the bar in fixed steps, like the frame loop does.
		void Tick(const float seconds, const float step = 1.0f / 60.0f) const
		{
			for (float t = 0.0f; t < seconds; t += step)
			{
				bar->Update(step);
			}
		}

		lua_State* lua = nullptr;
		Localization localization;
		std::shared_ptr<ProgressBar> bar;
	};
}

TEST_CASE_METHOD(ProgressBarFixture, "Progress bar jumps to its value without animation settings", "[frame_ui][progress_bar]")
{
	bar->SetProgress(0.75f);

	CHECK(bar->GetProgress() == Approx(0.75f));
	CHECK(bar->GetDisplayedProgress() == Approx(0.75f));
	CHECK(bar->GetTrailProgress() == Approx(0.75f));
	CHECK_FALSE(bar->IsAnimating());

	bar->SetProgress(0.25f);
	CHECK(bar->GetDisplayedProgress() == Approx(0.25f));
	CHECK(bar->GetTrailProgress() == Approx(0.25f));
	CHECK_FALSE(bar->IsAnimating());
}

TEST_CASE_METHOD(ProgressBarFixture, "Smoothed progress bar approaches its target and settles on it", "[frame_ui][progress_bar]")
{
	bar->SetProperty("SmoothSpeed", "10");
	bar->SetProgressInstant(0.0f);

	bar->SetProgress(1.0f);
	CHECK(bar->GetDisplayedProgress() == Approx(0.0f));
	CHECK(bar->IsAnimating());

	bar->Update(0.05f);
	CHECK(bar->GetDisplayedProgress() > 0.0f);
	CHECK(bar->GetDisplayedProgress() < 1.0f);

	// The approach is frame-rate independent: one long step lands where many short ones do.
	const float afterShortSteps = [&]
	{
		bar->SetProgressInstant(0.0f);
		bar->SetProgress(1.0f);
		for (int i = 0; i < 10; ++i)
		{
			bar->Update(0.01f);
		}
		return bar->GetDisplayedProgress();
	}();
	bar->SetProgressInstant(0.0f);
	bar->SetProgress(1.0f);
	bar->Update(0.1f);
	CHECK(bar->GetDisplayedProgress() == Approx(afterShortSteps).margin(0.001f));

	Tick(2.0f);
	CHECK(bar->GetDisplayedProgress() == Approx(1.0f));
	CHECK_FALSE(bar->IsAnimating());
}

TEST_CASE_METHOD(ProgressBarFixture, "Trail holds behind a loss, then drains onto the fill", "[frame_ui][progress_bar]")
{
	bar->SetProperty("TrailDelay", "0.3");
	bar->SetProperty("TrailSpeed", "1.0");
	bar->SetProgressInstant(1.0f);

	bar->SetProgress(0.4f);
	CHECK(bar->GetDisplayedProgress() == Approx(0.4f));
	CHECK(bar->GetTrailProgress() == Approx(1.0f));

	// Still holding.
	Tick(0.25f);
	CHECK(bar->GetTrailProgress() == Approx(1.0f));

	// Draining at one bar width per second: 0.6 to go, well done after a second.
	Tick(0.2f);
	CHECK(bar->GetTrailProgress() < 1.0f);
	CHECK(bar->GetTrailProgress() > 0.4f);

	Tick(1.0f);
	CHECK(bar->GetTrailProgress() == Approx(0.4f));
	CHECK_FALSE(bar->IsAnimating());
}

TEST_CASE_METHOD(ProgressBarFixture, "A new loss restarts the trail hold and keeps the highest trail", "[frame_ui][progress_bar]")
{
	bar->SetProperty("TrailDelay", "0.3");
	bar->SetProgressInstant(1.0f);

	bar->SetProgress(0.8f);
	Tick(0.2f);
	bar->SetProgress(0.5f);
	CHECK(bar->GetTrailProgress() == Approx(1.0f));

	// The hold restarted with the second hit, so the trail has not moved yet.
	Tick(0.2f);
	CHECK(bar->GetTrailProgress() == Approx(1.0f));
}

TEST_CASE_METHOD(ProgressBarFixture, "Gains never leave a trail", "[frame_ui][progress_bar]")
{
	bar->SetProperty("TrailDelay", "0.3");
	bar->SetProgressInstant(0.2f);

	bar->SetProgress(0.9f);
	CHECK(bar->GetTrailProgress() == Approx(0.9f));
	CHECK_FALSE(bar->IsAnimating());
}

TEST_CASE_METHOD(ProgressBarFixture, "Instant progress and hidden bars skip the animation", "[frame_ui][progress_bar]")
{
	bar->SetProperty("SmoothSpeed", "10");
	bar->SetProperty("TrailDelay", "0.3");
	bar->SetProgressInstant(1.0f);

	bar->SetProgressInstant(0.3f);
	CHECK(bar->GetDisplayedProgress() == Approx(0.3f));
	CHECK(bar->GetTrailProgress() == Approx(0.3f));
	CHECK_FALSE(bar->IsAnimating());

	bar->Hide();
	bar->SetProgress(0.9f);
	CHECK(bar->GetDisplayedProgress() == Approx(0.9f));
	CHECK(bar->GetTrailProgress() == Approx(0.9f));
	CHECK_FALSE(bar->IsAnimating());
}

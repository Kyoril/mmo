// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "loading_screen.h"

#include "screen.h"
#include "frame_ui/geometry_buffer.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"

namespace mmo
{
	static ScreenLayerIt s_paintLoadingScreenLayer;
	static std::shared_ptr<Texture> s_loadingTexture;
	static bool s_textureLoaded = false;

	static std::unique_ptr<GeometryBuffer> s_loadingScreenBuffer;

	signal<void()> LoadingScreen::LoadingScreenShown;

	void LoadingScreen::Init()
	{
		s_paintLoadingScreenLayer = Screen::AddLayer(
			&LoadingScreen::Paint, 
			1000.0f, 
			ScreenLayerFlags::Disabled | ScreenLayerFlags::IdentityProjection | ScreenLayerFlags::IdentityTransform);

		SetLoadingScreenTexture("Interface/Loading.htex");
	}

	void LoadingScreen::Destroy()
	{
		Screen::RemoveLayer(s_paintLoadingScreenLayer);

		s_loadingTexture.reset();
		s_textureLoaded = false;
	}

	void LoadingScreen::SetLoadingScreenTexture(const String& texture)
	{
		if (!s_loadingScreenBuffer)
		{
			s_loadingScreenBuffer = std::make_unique<GeometryBuffer>();
		}
		else
		{
			s_loadingScreenBuffer->Reset();
		}

		s_loadingTexture = TextureManager::Get().CreateOrRetrieve(texture);
		s_textureLoaded = true;

		if (s_loadingTexture)
		{
			s_loadingScreenBuffer->SetActiveTexture(s_loadingTexture);

			// Create the rectangle
			GeometryHelper::CreateRect(*s_loadingScreenBuffer,
				Color::White,
				Rect(-1.0f, -1.0f, 1.0f, 1.0f),
				Rect(0.0f, s_loadingTexture->GetHeight(), s_loadingTexture->GetWidth(), 0.0f),
				s_loadingTexture->GetWidth(), s_loadingTexture->GetHeight());
		}
	}

	void LoadingScreen::Paint()
	{
		if (s_loadingScreenBuffer)
		{
			s_loadingScreenBuffer->Draw();
		}

		// Trigger handlers
		LoadingScreenShown();
		LoadingScreenShown.clear();
	}

	void LoadingScreen::Show()
	{
		s_paintLoadingScreenLayer->flags &= ~ScreenLayerFlags::Disabled;
	}

	void LoadingScreen::Hide()
	{
		s_paintLoadingScreenLayer->flags |= ScreenLayerFlags::Disabled;
	}
}

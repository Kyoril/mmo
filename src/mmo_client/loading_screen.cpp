// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "loading_screen.h"

#include "client_locale.h"
#include "screen.h"
#include "frame_ui/font_mgr.h"
#include "frame_ui/geometry_buffer.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/graphics_device.h"
#include "graphics/render_window.h"
#include "graphics/texture_mgr.h"
#include "shared/client_data/proto_client/loading_tips.pb.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

namespace mmo
{
	namespace
	{
		/// The progress bar uses the BlueGold theme's bar pieces, like the class experience bar and nameplates.
		constexpr const char* BarWellTexturePath = "Interface/GameUI/Themes/BlueGold/Textures/NavyWellV4.htex";
		constexpr const char* BarFillTexturePath = "Interface/GameUI/Themes/BlueGold/Textures/BarFillV4.htex";
		constexpr const char* BarRimTexturePath = "Interface/GameUI/Themes/BlueGold/Textures/BarRimV4.htex";
		/// A solid white texture, so the vertex colour alone drives the backdrop gradient.
		constexpr const char* WhiteTexturePath = "Textures/Character/T_EV_BlankWhite_01.htex";
		constexpr const char* TipFontPath = "Fonts/FRIZQT__.TTF";

		/// Source slice size of the bar rim's nine-slice texture, as used by the theme.
		constexpr float BarRimSlice = 208.0f;

		/// Layout in pixels at a 1080 px high viewport; scaled with the viewport height.
		constexpr float ReferenceHeight = 1080.0f;
		constexpr float BarMaxWidth = 900.0f;
		constexpr float BarHeight = 26.0f;
		constexpr float BarBottomMargin = 70.0f;
		constexpr float BarRimSize = 11.0f;
		constexpr float BarFillInsetX = 7.0f;
		constexpr float BarFillInsetY = 7.0f;
		constexpr float BarGlowWidth = 28.0f;
		constexpr float BarGlowAlpha = 0.45f;
		constexpr int BarGlowSteps = 8;
		constexpr float TipBarSpacing = 22.0f;
		constexpr float TipShadowOffset = 2.0f;

		/// The backdrop darkens the bottom of the screen, fading in over BackdropFade above the tip text.
		constexpr float BackdropFade = 140.0f;
		constexpr float BackdropMaxAlpha = 0.72f;
		constexpr int BackdropFadeSteps = 32;

		const Color BarWellTint(0.30f, 0.38f, 0.47f, 1.0f);
		const Color BarFillTint(0.87f, 0.66f, 0.24f, 1.0f);
		const Color BarRimTint(0.83f, 0.77f, 0.65f, 1.0f);
		const Color TipColor(1.0f, 0.86f, 0.55f, 1.0f);
		const Color TipShadowColor(0.0f, 0.0f, 0.0f, 0.85f);

		/// Minimum time between two frames presented from inside blocking loading code.
		constexpr auto PresentInterval = std::chrono::milliseconds(100);

		/// Splits a text into lines no wider than maxWidth, breaking at spaces (and at explicit newlines).
		std::vector<String> WrapText(Font& font, const String& text, const float maxWidth, const float scale)
		{
			std::vector<String> lines;
			String line;

			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find_first_of(" \n", start);
				if (end == String::npos)
				{
					end = text.size();
				}

				const String word = text.substr(start, end - start);
				if (!word.empty())
				{
					const String candidate = line.empty() ? word : line + " " + word;
					if (!line.empty() && font.GetTextWidth(candidate, scale) > maxWidth)
					{
						lines.push_back(line);
						line = word;
					}
					else
					{
						line = candidate;
					}
				}

				if (end < text.size() && text[end] == '\n')
				{
					lines.push_back(line);
					line.clear();
				}

				start = end + 1;
			}

			if (!line.empty())
			{
				lines.push_back(line);
			}

			return lines;
		}

		/// Draws a texture as nine slices into dst: the corners stay square at edgeSize, edges and centre stretch.
		void CreateNineSlice(GeometryBuffer& buffer, const TexturePtr& texture, const argb_t color, const Rect& dst, const float sliceSize, const float edgeSize)
		{
			const float texW = texture->GetWidth();
			const float texH = texture->GetHeight();
			const float edge = std::min(edgeSize, std::min(dst.GetWidth(), dst.GetHeight()) * 0.5f);

			const float dstX[4] = { dst.left, dst.left + edge, dst.right - edge, dst.right };
			const float dstY[4] = { dst.top, dst.top + edge, dst.bottom - edge, dst.bottom };
			const float srcX[4] = { 0.0f, sliceSize, texW - sliceSize, texW };
			const float srcY[4] = { 0.0f, sliceSize, texH - sliceSize, texH };

			buffer.SetActiveTexture(texture);
			for (int y = 0; y < 3; ++y)
			{
				for (int x = 0; x < 3; ++x)
				{
					if (dstX[x + 1] <= dstX[x] || dstY[y + 1] <= dstY[y])
					{
						continue;
					}

					GeometryHelper::CreateRect(buffer, color,
						Rect(dstX[x], dstY[y], dstX[x + 1], dstY[y + 1]),
						Rect(srcX[x], srcY[y], srcX[x + 1], srcY[y + 1]),
						texture->GetWidth(), texture->GetHeight());
				}
			}
		}
	}

	static ScreenLayerIt s_paintLoadingScreenLayer;
	static std::shared_ptr<Texture> s_loadingTexture;
	static bool s_textureLoaded = false;

	static std::unique_ptr<GeometryBuffer> s_loadingScreenBuffer;

	static TexturePtr s_barWellTexture;
	static TexturePtr s_barFillTexture;
	static TexturePtr s_barRimTexture;
	static TexturePtr s_whiteTexture;
	static FontPtr s_tipFont;
	static std::unique_ptr<GeometryBuffer> s_barBuffer;
	static std::unique_ptr<GeometryBuffer> s_tipBuffer;

	static const proto_client::LoadingTips* s_tips = nullptr;
	static String s_currentTip;
	static bool s_tipDirty = true;
	static int32 s_tipViewportWidth = 0;
	static int32 s_tipViewportHeight = 0;
	static size_t s_tipLineCount = 0;

	static float s_progress = 0.0f;
	static std::chrono::steady_clock::time_point s_lastPresent;

	signal<void()> LoadingScreen::LoadingScreenShown;

	void LoadingScreen::Init()
	{
		s_paintLoadingScreenLayer = Screen::AddLayer(
			&LoadingScreen::Paint,
			1000.0f,
			ScreenLayerFlags::Disabled | ScreenLayerFlags::IdentityProjection | ScreenLayerFlags::IdentityTransform);

		SetLoadingScreenTexture("Interface/Loading.htex");

		s_barWellTexture = TextureManager::Get().CreateOrRetrieve(BarWellTexturePath);
		s_barFillTexture = TextureManager::Get().CreateOrRetrieve(BarFillTexturePath);
		s_barRimTexture = TextureManager::Get().CreateOrRetrieve(BarRimTexturePath);
		s_whiteTexture = TextureManager::Get().CreateOrRetrieve(WhiteTexturePath);
		s_tipFont = FontManager::Get().CreateOrRetrieve(TipFontPath, 20.0f, 1.0f);
		s_barBuffer = std::make_unique<GeometryBuffer>();
		s_tipBuffer = std::make_unique<GeometryBuffer>();
	}

	void LoadingScreen::Destroy()
	{
		Screen::RemoveLayer(s_paintLoadingScreenLayer);

		s_loadingTexture.reset();
		s_textureLoaded = false;

		s_barBuffer.reset();
		s_tipBuffer.reset();
		s_tipFont.reset();
		s_barWellTexture.reset();
		s_barFillTexture.reset();
		s_barRimTexture.reset();
		s_whiteTexture.reset();
		s_tips = nullptr;
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

	void LoadingScreen::SetTips(const proto_client::LoadingTips* tips)
	{
		s_tips = tips;
	}

	void LoadingScreen::PaintContent()
	{
		auto& gx = GraphicsDevice::Get();
		gx.SetTopologyType(TopologyType::TriangleList);
		gx.SetBlendMode(BlendMode::Alpha);

		if (s_loadingScreenBuffer)
		{
			s_loadingScreenBuffer->Draw();
		}

		int32 vpWidth = 0, vpHeight = 0;
		gx.GetViewport(nullptr, nullptr, &vpWidth, &vpHeight);
		if (vpWidth <= 0 || vpHeight <= 0)
		{
			return;
		}

		// Everything below is laid out in pixels
		gx.SetTransformMatrix(TransformType::Projection,
			gx.MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(vpWidth), static_cast<float>(vpHeight), 0.0f, 100.0f));
		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);

		const float scale = std::clamp(static_cast<float>(vpHeight) / ReferenceHeight, 0.5f, 2.0f);
		const float barWidth = std::min(static_cast<float>(vpWidth) * 0.6f, BarMaxWidth * scale);
		const float barLeft = (static_cast<float>(vpWidth) - barWidth) * 0.5f;
		const float barBottom = static_cast<float>(vpHeight) - BarBottomMargin * scale;
		const float barTop = barBottom - BarHeight * scale;

		// Build the tip text first: the backdrop behind it depends on its line count
		const bool hasTip = s_tipFont && s_tipBuffer && !s_currentTip.empty();
		if (hasTip && (s_tipDirty || vpWidth != s_tipViewportWidth || vpHeight != s_tipViewportHeight))
		{
			s_tipBuffer->Reset();

			const std::vector<String> lines = WrapText(*s_tipFont, s_currentTip, barWidth, scale);
			const float lineHeight = s_tipFont->GetHeight(scale);
			const float shadowOffset = TipShadowOffset * scale;
			float y = barTop - TipBarSpacing * scale - lineHeight * static_cast<float>(lines.size());

			for (const String& line : lines)
			{
				const float x = (static_cast<float>(vpWidth) - s_tipFont->GetTextWidth(line, scale)) * 0.5f;
				s_tipFont->DrawText(line, Point(x + shadowOffset, y + shadowOffset), *s_tipBuffer, scale, TipShadowColor.GetARGB());
				s_tipFont->DrawText(line, Point(x, y), *s_tipBuffer, scale, TipColor.GetARGB());
				y += lineHeight;
			}

			s_tipLineCount = lines.size();
			s_tipDirty = false;
			s_tipViewportWidth = vpWidth;
			s_tipViewportHeight = vpHeight;
		}

		if (s_barBuffer)
		{
			s_barBuffer->Reset();

			// Darken the bottom of the screen behind tip and bar, so both stay readable on bright loading
			// screen art: solid from the tip text down, fading out smoothly above it
			if (s_whiteTexture)
			{
				const float textHeight = hasTip ? s_tipFont->GetHeight(scale) * static_cast<float>(s_tipLineCount) + TipBarSpacing * scale : 0.0f;
				const float solidTop = barTop - textHeight - 16.0f * scale;
				const float fade = BackdropFade * scale;
				const float right = static_cast<float>(vpWidth);

				s_barBuffer->SetActiveTexture(s_whiteTexture);
				for (int i = 0; i < BackdropFadeSteps; ++i)
				{
					const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(BackdropFadeSteps);
					const float alpha = BackdropMaxAlpha * t * t * (3.0f - 2.0f * t);
					const float top = solidTop - fade + fade * static_cast<float>(i) / static_cast<float>(BackdropFadeSteps);
					const float bottom = solidTop - fade + fade * static_cast<float>(i + 1) / static_cast<float>(BackdropFadeSteps);
					GeometryHelper::CreateRect(*s_barBuffer, Color(0.0f, 0.0f, 0.0f, alpha).GetARGB(), Rect(0.0f, top, right, bottom));
				}

				GeometryHelper::CreateRect(*s_barBuffer, Color(0.0f, 0.0f, 0.0f, BackdropMaxAlpha).GetARGB(),
					Rect(0.0f, solidTop, right, static_cast<float>(vpHeight)));
			}

			const Rect barRect(barLeft, barTop, barLeft + barWidth, barBottom);
			const Rect fillArea(barLeft + BarFillInsetX * scale, barTop + BarFillInsetY * scale, barLeft + barWidth - BarFillInsetX * scale, barBottom - BarFillInsetY * scale);

			if (s_barWellTexture)
			{
				s_barBuffer->SetActiveTexture(s_barWellTexture);
				GeometryHelper::CreateRect(*s_barBuffer, BarWellTint.GetARGB(), fillArea,
					Rect(0.0f, 0.0f, s_barWellTexture->GetWidth(), s_barWellTexture->GetHeight()),
					s_barWellTexture->GetWidth(), s_barWellTexture->GetHeight());
			}

			if (s_barFillTexture && s_progress > 0.0f)
			{
				// Reveal the fill texture instead of squeezing it, so its shading stays put while the bar grows
				Rect fill = fillArea;
				fill.right = fill.left + fillArea.GetWidth() * s_progress;

				s_barBuffer->SetActiveTexture(s_barFillTexture);
				GeometryHelper::CreateRect(*s_barBuffer, BarFillTint.GetARGB(), fill,
					Rect(0.0f, 0.0f, s_barFillTexture->GetWidth() * s_progress, s_barFillTexture->GetHeight()),
					s_barFillTexture->GetWidth(), s_barFillTexture->GetHeight());

				// Soft highlight at the leading edge of the fill
				if (s_whiteTexture && s_progress < 1.0f)
				{
					const float glowWidth = std::min(BarGlowWidth * scale, fill.GetWidth());
					s_barBuffer->SetActiveTexture(s_whiteTexture);
					for (int i = 0; i < BarGlowSteps; ++i)
					{
						const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(BarGlowSteps);
						const float left = fill.right - glowWidth + glowWidth * static_cast<float>(i) / static_cast<float>(BarGlowSteps);
						const float right = fill.right - glowWidth + glowWidth * static_cast<float>(i + 1) / static_cast<float>(BarGlowSteps);
						GeometryHelper::CreateRect(*s_barBuffer, Color(1.0f, 0.95f, 0.75f, BarGlowAlpha * t * t).GetARGB(), Rect(left, fill.top, right, fill.bottom));
					}
				}
			}

			if (s_barRimTexture)
			{
				CreateNineSlice(*s_barBuffer, s_barRimTexture, BarRimTint.GetARGB(), barRect, BarRimSlice, BarRimSize * scale);
			}

			s_barBuffer->Draw();
		}

		if (hasTip)
		{
			s_tipBuffer->Draw();
		}
	}

	void LoadingScreen::Paint()
	{
		PaintContent();

		// Trigger handlers
		LoadingScreenShown();
		LoadingScreenShown.clear();
	}

	void LoadingScreen::Show()
	{
		if (IsVisible())
		{
			return;
		}

		s_paintLoadingScreenLayer->flags &= ~ScreenLayerFlags::Disabled;
		s_progress = 0.0f;

		// Pick a random tip which has text in the client's language (or base text to fall back to)
		s_currentTip.clear();
		s_tipDirty = true;
		if (s_tips)
		{
			std::vector<const String*> candidates;
			candidates.reserve(s_tips->entry_size());
			for (const auto& tip : s_tips->entry())
			{
				const String& text = GetLocalizedString(tip.text(), tip.text_loc(), GetClientLocale());
				if (!text.empty())
				{
					candidates.push_back(&text);
				}
			}

			if (!candidates.empty())
			{
				static std::mt19937 s_random{ std::random_device{}() };
				std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
				s_currentTip = *candidates[pick(s_random)];
			}
		}
	}

	void LoadingScreen::Hide()
	{
		s_paintLoadingScreenLayer->flags |= ScreenLayerFlags::Disabled;
	}

	bool LoadingScreen::IsVisible()
	{
		return (s_paintLoadingScreenLayer->flags & ScreenLayerFlags::Disabled) == 0;
	}

	void LoadingScreen::SetProgress(const float progress)
	{
		s_progress = std::max(s_progress, std::clamp(progress, 0.0f, 1.0f));
	}

	void LoadingScreen::Present(const bool force)
	{
		if (!IsVisible() || Screen::IsPainting())
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (!force && now - s_lastPresent < PresentInterval)
		{
			return;
		}

		s_lastPresent = now;

		auto& gx = GraphicsDevice::Get();
		const auto window = gx.GetAutoCreatedWindow();
		if (!window)
		{
			return;
		}

		// Same frame setup as the event loop, but only the loading screen is drawn: the world behind it
		// is in the middle of being loaded.
		gx.Reset();
		window->Activate();
		window->Clear(ClearFlags::All);

		gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
		gx.SetTransformMatrix(TransformType::Projection, Matrix4::Identity);
		PaintContent();

		window->Update();
	}
}

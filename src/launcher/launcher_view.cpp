// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_view.h"

#include "canvas.h"
#include "launcher_layout.h"
#include "resource_data.h"
#include "theme.h"
#include "version.h"

#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Every image the skin needs. Decode once so opening a page does not stall
		/// while loading its artwork.
		constexpr std::array<uint32, 15> RequiredImages = {
			IDR_PNG_SPLASH,			 IDR_PNG_PANEL_BOTTOM,	  IDR_PNG_BORDER_FRAME,		IDR_PNG_BUTTON_UP,	   IDR_PNG_BUTTON_OVER,
			IDR_PNG_BUTTON_DOWN,	 IDR_PNG_BUTTON_DISABLED, IDR_PNG_PROGRESS_TRACK,	IDR_PNG_PROGRESS_FILL, IDR_PNG_ICON_CLOSE,
			IDR_PNG_CONTENT_TEXTURE, IDR_PNG_HERO_FRAME,	  IDR_PNG_NEWS_DEVELOPMENT, IDR_PNG_NEWS_WORLD, IDR_PNG_BORDER_REFINED};

		/// Moves `current` toward `target` at `rate` per second, framerate independently.
		float EaseTowards(const float current, const float target, const float rate, const float deltaSeconds)
		{
			const float t = 1.0f - std::exp(-rate * deltaSeconds);
			return current + (target - current) * t;
		}
		/// Small vector silhouettes rasterized with subpixel coverage at the current DPI.
		Bitmap makeSectionIcon(const size_t index, const int32 size)
		{
			Bitmap bitmap(size, size);
			constexpr int samples = 4;
			for (int32 y = 0; y < size; ++y)
			{
				for (int32 x = 0; x < size; ++x)
				{
					int coverage = 0;
					for (int sy = 0; sy < samples; ++sy)
					{
						for (int sx = 0; sx < samples; ++sx)
						{
							const float px = (x + (sx + 0.5f) / samples) * 24.0f / size - 12.0f;
							const float py = (y + (sy + 0.5f) / samples) * 24.0f / size - 12.0f;
							const float radius = std::sqrt(px * px + py * py);
							bool inside = false;
							if (index == 0)
							{
								inside = (std::abs(px) / 2.0f + std::abs(py) / 11.0f <= 1.0f)
									|| (std::abs(px) / 11.0f + std::abs(py) / 2.0f <= 1.0f)
									|| std::abs(radius - 6.2f) < 0.85f;
								if (radius < 2.6f && radius > 1.2f)
								{
									inside = false;
								}
							}
							else if (index == 1)
							{
								const float edge = std::cos(std::atan2(py, px) * 8.0f) > 0.25f ? 10.2f : 8.2f;
								inside = radius <= edge && radius >= 3.2f;
							}
							else
							{
								const float u = (px + py) * 0.70710678f;
								const float v = (py - px) * 0.70710678f;
								const float jaw = std::sqrt(u * u + (v + 6.0f) * (v + 6.0f));
								const float handle = std::sqrt(u * u + (v - 7.8f) * (v - 7.8f));
								inside = (jaw < 4.5f && !(v < -6.0f && std::abs(u) < 2.4f))
									|| (std::abs(u) < 1.8f && v > -5.5f && v < 8.0f)
									|| (handle < 2.8f && handle > 1.0f);
								if (handle < 1.0f)
								{
									inside = false;
								}
							}
							coverage += inside ? 1 : 0;
						}
					}
					bitmap.GetRow(y)[x] = MakeColor(255, static_cast<uint8>(220 - y * 32 / size), 112,
						static_cast<uint8>(coverage * 255 / (samples * samples)));
				}
			}
			return bitmap;
		}

	}

	LauncherView::LauncherView(IPlatformHost& host)
		: m_host(host)
	{
	}

	int32 LauncherView::Scale(const int32 logical) const
	{
		return static_cast<int32>(static_cast<float>(logical) * m_dpiScale + 0.5f);
	}

	Rect LauncherView::Scale(const Rect& logical) const
	{
		return Rect{ Scale(logical.left), Scale(logical.top), Scale(logical.right), Scale(logical.bottom) };
	}

	Insets LauncherView::Scale(const Insets& logical) const
	{
		return Insets{ Scale(logical.left), Scale(logical.top), Scale(logical.right), Scale(logical.bottom) };
	}

	bool LauncherView::Initialize(const float dpiScale)
	{
		m_dpiScale = dpiScale;

		if (!LoadAssets())
		{
			return false;
		}

		m_titleLabel.rect = layout::TitleText;
		m_titleLabel.text = "ALESTIA ONLINE";
		m_titleLabel.align = TextAlign::Left;
		m_titleLabel.color = theme::TitleColor;

		m_versionLabel.rect = layout::VersionText;
		m_versionLabel.text = MMO_VERSION_STR;
		m_versionLabel.align = TextAlign::Right;
		m_versionLabel.color = theme::VersionColor;

		m_heroTitleLabel.rect = layout::HeroTitle;
		m_heroTitleLabel.text = "ENTER THE WORLD OF ALESTIA";
		m_heroTitleLabel.align = TextAlign::Left;
		m_heroTitleLabel.color = theme::HeroTitleColor;

		m_heroSubtitleLabel.rect = layout::HeroSubtitle;
		m_heroSubtitleLabel.text = "YOUR ADVENTURE AWAITS";
		m_heroSubtitleLabel.align = TextAlign::Left;
		m_heroSubtitleLabel.color = theme::HeroSubtitleColor;

		m_statusLabel.rect = layout::StatusLabel;
		m_statusLabel.text = "Checking for updates...";
		m_statusLabel.align = TextAlign::Left;
		m_statusLabel.color = theme::StatusColor;

		m_percentLabel.rect = layout::PercentText;
		m_percentLabel.align = TextAlign::Right;
		m_percentLabel.color = theme::PercentColor;

		m_noticeLabel.rect = layout::NoticeLabel;
		m_noticeLabel.align = TextAlign::Left;
		m_noticeLabel.color = theme::StatusWarningColor;

		m_progress.rect = layout::ProgressTrack;

		m_playButton.rect = layout::PlayButton;
		m_playButton.text = "PLAY";
		// Disabled until the update finishes, which makes it the first thing anyone
		// sees; the Disabled art matters as much as the Up art.
		m_playButton.enabled = false;
		m_playButton.onClick = [this] { m_host.LaunchGame(); };
		m_pauseButton.rect = layout::PauseButton;
		m_pauseButton.enabled = false;
		m_pauseButton.onClick = [this]
		{
			if (m_host.SetDownloadPaused(!m_paused))
			{
				m_paused = !m_paused;
				m_dirty = true;
			}
		};

		m_minimizeButton.rect = layout::MinimizeButton;
		m_minimizeButton.iconId = IDR_PNG_ICON_MINIMIZE;
		m_minimizeButton.onClick = [this] { m_host.Minimize(); };

		m_closeButton.rect = layout::CloseButton;
		m_closeButton.iconId = IDR_PNG_ICON_CLOSE;
		m_closeButton.onClick = [this] { m_host.Close(); };

		m_content.Load(m_host.GetGameDirectory());
		m_keepOpen = m_host.GetKeepOpen();
		const std::array<const char*, 4> tabs{"Home", "News", "Patch Notes", "Settings"};
		for (size_t i = 0; i < m_navigation.size(); ++i)
		{
			Button& button = m_navigation[i];
			button.rect = Rect{390 + static_cast<int32>(i) * 124, 50, 506 + static_cast<int32>(i) * 124, 98};
			button.text = tabs[i];
			button.onClick = [this, i]
			{
				SelectPage(static_cast<Page>(i));
			};
		}
		BuildPageButtons();
		SetDpiScale(dpiScale);
		return true;
	}

	bool LauncherView::LoadAssets()
	{
		for (const uint32 id : RequiredImages)
		{
			Bitmap bitmap;
			if (!Bitmap::DecodePng(LoadResourceBlob(id), bitmap))
			{
				ELOG("Failed to decode embedded image " << id);
				return false;
			}

			m_assets.emplace(id, std::move(bitmap));
		}

		Bitmap minimizeIcon;
		if (!Bitmap::DecodePng(LoadResourceBlob(IDR_PNG_ICON_MINIMIZE), minimizeIcon))
		{
			ELOG("Failed to decode the minimize icon");
			return false;
		}

		m_assets.emplace(IDR_PNG_ICON_MINIMIZE, std::move(minimizeIcon));
		return true;
	}

	bool LauncherView::BuildFonts()
	{
		const ResourceBlob display = LoadResourceBlob(IDR_TTF_DISPLAY);
		const ResourceBlob body = LoadResourceBlob(IDR_TTF_BODY);
		if (!display.IsValid() || !body.IsValid())
		{
			ELOG("Embedded fonts are missing");
			return false;
		}

		// Faces are rebuilt rather than rescaled: FreeType stores the size on the face,
		// so a new size means a new face, and the glyph caches go with them.
		const auto build = [](const ResourceBlob& blob, const int32 pixelHeight) -> std::unique_ptr<FontFace>
		{
			auto face = std::make_unique<FontFace>();
			if (!face->Initialize(blob, pixelHeight))
			{
				return nullptr;
			}

			return face;
		};

		m_titleFont = build(display, Scale(theme::TitleFontSize));
		m_headingFont = build(body, Scale(24));
		m_sectionFont = build(body, Scale(18));
		m_heroTitleFont = build(display, Scale(theme::HeroTitleFontSize));
		m_heroSubtitleFont = build(body, Scale(theme::HeroSubtitleFontSize));
		m_playFont = build(display, Scale(theme::PlayFontSize));
		m_versionFont = build(body, Scale(theme::VersionFontSize));
		m_statusFont = build(body, Scale(theme::StatusFontSize));
		m_percentFont = build(body, Scale(theme::PercentFontSize));

		if (!m_titleFont || !m_headingFont || !m_sectionFont || !m_heroTitleFont || !m_heroSubtitleFont || !m_playFont || !m_versionFont || !m_statusFont ||
			!m_percentFont)
		{
			ELOG("Failed to build the launcher fonts");
			return false;
		}

		return true;
	}

	void LauncherView::SetDpiScale(const float dpiScale)
	{
		m_dpiScale = dpiScale;

		BuildFonts();

		const int32 iconSize = Scale(layout::TitleIconSize);
		if (const Bitmap* icon = GetAsset(IDR_PNG_ICON_CLOSE); icon && icon->IsValid())
		{
			Bitmap::Resample(*icon, iconSize, iconSize, m_closeIcon);
		}

		if (const Bitmap* icon = GetAsset(IDR_PNG_ICON_MINIMIZE); icon && icon->IsValid())
		{
			Bitmap::Resample(*icon, iconSize, iconSize, m_minimizeIcon);
		}

		// This asset has transparent export padding. Preserve the measured corner art,
		// then scale the source as well as its nine-slice insets for consistent DPI.
		if (const Bitmap* frame = GetAsset(IDR_PNG_BORDER_REFINED))
		{
			const Rect source{ 40, 71, 1521, 928 };
			Bitmap trimmed(source.GetWidth(), source.GetHeight());
			Canvas trimCanvas(trimmed.GetPixels(), trimmed.GetWidth(), trimmed.GetHeight(), trimmed.GetWidth());
			trimCanvas.Clear(theme::Transparent);
			trimCanvas.Blit(*frame, source, trimmed.GetBounds(), BlitFilter::Nearest);
			Bitmap::Resample(trimmed, Scale(963), Scale(557), m_windowFrameScaled);
		}
		for (size_t i = 0; i < m_sectionIcons.size(); ++i)
		{
			m_sectionIcons[i] = makeSectionIcon(i, Scale(24));
		}

		RebuildBackground();
		m_pageDirty = true;
		m_dirty = true;
	}

	const Bitmap* LauncherView::GetAsset(const uint32 resourceId) const
	{
		const auto it = m_assets.find(resourceId);
		return it == m_assets.end() ? nullptr : &it->second;
	}

	void LauncherView::RebuildBackground()
	{
		const int32 surfaceWidth = Scale(layout::WindowWidth);
		const int32 surfaceHeight = Scale(layout::WindowHeight);

		m_background = Bitmap(surfaceWidth, surfaceHeight);

		Canvas canvas(m_background.GetPixels(), surfaceWidth, surfaceHeight, surfaceWidth);

		// The window is layered, so anything left untouched is a hole through to the
		// desktop. That is what lets the frame's ornamental silhouette read against
		// whatever is behind the launcher instead of against a dark plate.
		canvas.Clear(theme::Transparent);

		// Everything inside the frame's opening is opaque; the frame art itself covers
		// the band around it. Clipping here is what keeps the corners and the gaps
		// between the edge ornaments transparent.
		canvas.PushClip(Scale(layout::Content));

		canvas.Clear(theme::WindowBackground);

		if (const Bitmap* texture = GetAsset(IDR_PNG_CONTENT_TEXTURE))
		{
			// The stone source includes a narrow presentation bevel. Crop to the quiet
			// mineral center so it behaves as a surface texture rather than a second frame.
			const Rect textureCenter = Inflate(texture->GetBounds(), -8);
			canvas.Blit(*texture, textureCenter, Scale(layout::Content));
			canvas.FillRect(Scale(layout::Content), theme::ContentShade);
		}

		// The title row is deliberately opaque window chrome. Its full-surface value
		// change establishes the boundary; the narrow bronze lip only finishes the edge.
		canvas.FillGradient(Scale(layout::TitleBarPlate),
			theme::TitleBarFrom, theme::TitleBarTo, false);
		canvas.FillRect(Scale(layout::TitleBarEdge), theme::TitleBarEdge);
		canvas.FillGradient(Scale(layout::TitleBarShadow),
			theme::TitleBarShadowFrom, theme::TitleBarShadowTo, false);

		canvas.DrawVignette(Scale(layout::Content), layout::VignetteStrength);

		canvas.FillGradient(Scale(layout::BottomPanel), FromArgb(0xE32C2418), FromArgb(0xF008090A), false);
		canvas.FillRect(Scale(Rect{32, 592, 1108, 594}), FromArgb(0xFF867044));
		canvas.FillRect(Scale(Rect{32, 594, 1108, 595}), FromArgb(0x403F3019));
		canvas.FillGradient(Scale(Rect{32, 690, 1108, 708}), FromArgb(0x00291E10), FromArgb(0x904A3619), false);
		canvas.FillRect(Scale(Rect{844, 611, 845, 682}), FromArgb(0x604F412B));
		canvas.FillRect(Scale(Rect{644, 611, 645, 682}), FromArgb(0xA087704A));
		canvas.FillRect(Scale(Rect{645, 611, 646, 682}), FromArgb(0x70000000));

		canvas.PopClip();

		DrawWindowFrame(canvas);
	}

	void LauncherView::DrawWindowFrame(Canvas& canvas)
	{
		if (m_windowFrameScaled.IsValid())
		{
			canvas.DrawNineSlice(m_windowFrameScaled, Scale(theme::WindowBorder.insets),
				Scale(layout::WindowFrame), Color{ 255, 255, 255, 255 }, false, NineSliceFill::FrameOnly);
		}
	}

	void LauncherView::ApplySnapshot(const UpdateSnapshot& snapshot)
	{
		if (snapshot.phase != UpdatePhase::Ready)
		{
			m_contentRefreshed = false;
		}
		else if (!m_contentRefreshed)
		{
			if (!m_remoteContent)
			{
				m_content.Load(m_host.GetGameDirectory());
			}
			m_contentRefreshed = true;
			m_selectedArticle = -1;
			m_selectedPatch = 0;
			m_newsOffset = 0;
			m_patchOffset = 0;
			m_scroll = 0;
			BuildPageButtons();
		}
		m_statusLabel.text = snapshot.paused ? "Download paused" : snapshot.statusText;
		m_paused = snapshot.paused;
		m_pauseButton.enabled = snapshot.phase == UpdatePhase::Updating;
		m_statusLabel.color = snapshot.phase == UpdatePhase::Failed
			? theme::StatusErrorColor
			: theme::StatusColor;

		m_noticeLabel.text = snapshot.noticeText;
		m_downloadSizeText = snapshot.phase == UpdatePhase::Updating ? snapshot.downloadSizeText : std::string();
		m_downloadRateText = snapshot.phase == UpdatePhase::Updating ? snapshot.downloadRateText : std::string();

		m_playButton.enabled = snapshot.playEnabled;

		// A negative progress means the total is not known yet, which is normal during
		// the prepare phase. Hide the bar rather than showing a false zero.
		m_progressIndeterminate = snapshot.progress < 0.0f;
		m_progress.target = m_progressIndeterminate ? 0.0f : snapshot.progress;

		m_percentLabel.text = m_progressIndeterminate
			? std::string()
			: std::to_string(static_cast<int32>(snapshot.progress * 100.0f + 0.5f)) + "%";

		m_dirty = true;
	}

	void LauncherView::ApplyRemoteContent(std::shared_ptr<const RemoteLauncherContent> content)
	{
		if (!content || !content->manifest)
		{
			return;
		}
		const bool changed = !m_remoteContent || m_remoteContent->manifest->revision != content->manifest->revision;
		m_remoteContent = std::move(content);
		if (changed)
		{
			m_content.Load(m_host.GetGameDirectory());
			m_content.Apply(m_remoteContent->manifest->news, m_remoteContent->manifest->patches);
			m_selectedArticle = -1;
			m_selectedPatch = 0;
			m_newsOffset = 0;
			m_patchOffset = 0;
			m_scroll = 0;
			m_heroTitleLabel.text = m_remoteContent->manifest->heroTitle.empty() ? "ENTER THE WORLD OF ALESTIA" : m_remoteContent->manifest->heroTitle;
			m_heroSubtitleLabel.text = m_remoteContent->manifest->heroSubtitle.empty() ? "YOUR ADVENTURE AWAITS" : m_remoteContent->manifest->heroSubtitle;
			BuildPageButtons();
		}
		m_pageDirty = true;
		m_dirty = true;
	}

	bool LauncherView::Tick(const float deltaSeconds)
	{
		bool moved = false;

		for (Button* button : GetButtons())
		{
			const float target = button->hovered && button->enabled ? 1.0f : 0.0f;
			if (std::abs(button->hoverFade - target) > 0.001f)
			{
				button->hoverFade = EaseTowards(button->hoverFade, target,
					1.0f / theme::HoverFadeDuration * 3.0f, deltaSeconds);
				moved = true;
			}
			else if (button->hoverFade != target)
			{
				button->hoverFade = target;
				moved = true;
			}
		}

		if (std::abs(m_progress.value - m_progress.target) > 0.0005f)
		{
			m_progress.value = EaseTowards(m_progress.value, m_progress.target,
				theme::ProgressEaseRate, deltaSeconds);
			moved = true;
		}
		else if (m_progress.value != m_progress.target)
		{
			m_progress.value = m_progress.target;
			moved = true;
		}

		return moved;
	}

	bool LauncherView::HitTestCaption(const Point& logical) const
	{
		// The title strip drags, and so does the frame band itself: with an ornate
		// border around the window, the whole frame reads as grabbable chrome.
		const bool onChrome = Rect(layout::Caption).Contains(logical)
			|| !Rect(layout::Content).Contains(logical);

		if (!onChrome)
		{
			return false;
		}

		// The buttons sit inside the caption strip, so they must be carved out or the
		// window would start dragging instead of closing.
		for (const Button& button : m_navigation)
		{
			if (button.rect.Contains(logical))
			{
				return false;
			}
		}
		return !m_closeButton.rect.Contains(logical) && !m_minimizeButton.rect.Contains(logical);
	}

	Button* LauncherView::FindButtonAt(const Point& logical)
	{
		for (Button* button : GetButtons())
		{
			if (button->enabled && button->rect.Contains(logical))
			{
				return button;
			}
		}

		return nullptr;
	}

	bool LauncherView::HasControlAt(const Point& logical)
	{
		return FindButtonAt(logical) != nullptr;
	}

	void LauncherView::OnMouseMove(const Point& logical)
	{
		const Button* hit = FindButtonAt(logical);

		for (Button* button : GetButtons())
		{
			const bool hovered = button == hit;
			if (button->hovered != hovered)
			{
				button->hovered = hovered;
				m_dirty = true;
			}
		}
	}

	void LauncherView::OnMouseLeave()
	{
		for (Button* button : GetButtons())
		{
			if (button->hovered || button->pressed)
			{
				button->hovered = false;
				button->pressed = false;
				m_dirty = true;
			}
		}
	}

	void LauncherView::OnMouseDown(const Point& logical)
	{
		if (Button* hit = FindButtonAt(logical))
		{
			hit->pressed = true;
			m_dirty = true;
		}
	}

	void LauncherView::OnMouseUp(const Point& logical)
	{
		for (Button* button : GetButtons())
		{
			const bool wasPressed = button->pressed;
			button->pressed = false;

			if (wasPressed)
			{
				m_dirty = true;
			}

			// A click only counts if the release lands on the same button that was
			// pressed, so pressing and dragging away cancels.
			if (wasPressed && button->enabled && button->rect.Contains(logical) && button->onClick)
			{
				auto action = button->onClick;
				action();
				return;
			}
		}
	}

	void LauncherView::DrawLabel(Canvas& canvas, FontFace& face, const Label& label,
		const TextStyle& style)
	{
		if (label.text.empty())
		{
			return;
		}

		TextStyle resolved = style;
		resolved.color = label.color;

		const Rect area = Scale(label.rect);

		// Status text is whatever the network stack produced and can be arbitrarily
		// long, so it is shortened to fit. The clip is the backstop: without it, an
		// overlong string would draw straight across the panel and under the PLAY
		// button, since DrawText does not clip on its own.
		const std::string text = ElideText(face, label.text, area.GetWidth());

		canvas.PushClip(area);
		DrawText(canvas, face, text, area, label.align, resolved);
		canvas.PopClip();
	}

	void LauncherView::DrawButton(Canvas& canvas, const Button& button)
	{
		const Rect rect = Scale(button.rect);

		if (button.iconId != 0)
		{
			// The titlebar buttons have no frame art: a rounded wash is cheaper than an
			// asset and reads the same at this size.
			const uint8 fade = static_cast<uint8>(std::clamp(button.hoverFade, 0.0f, 1.0f) * 255.0f + 0.5f);
			if (fade > 0)
			{
				const Color wash = button.GetState() == ButtonState::Pressed
					? theme::IconButtonPressed
					: ScaleColor(theme::IconButtonHover, fade);
				canvas.FillRoundedRect(rect, Scale(4), wash);
			}

			const Bitmap& icon = button.iconId == IDR_PNG_ICON_CLOSE ? m_closeIcon : m_minimizeIcon;
			if (icon.IsValid())
			{
				const int32 x = rect.left + (rect.GetWidth() - icon.GetWidth()) / 2;
				const int32 y = rect.top + (rect.GetHeight() - icon.GetHeight()) / 2;

				canvas.BlitTinted(icon, icon.GetBounds(),
					Rect{ x, y, x + icon.GetWidth(), y + icon.GetHeight() },
					theme::IconTint, BlitFilter::Nearest);
			}

			return;
		}

		const bool highlighted = button.enabled && button.hovered;
		canvas.FillRoundedRect(rect, Scale(4), FromArgb(highlighted ? 0xFFB88C3D : 0xFF665331));
		const Rect inner = Inflate(rect, -Scale(1));
		canvas.FillRoundedRect(inner, Scale(3), FromArgb(0xFF171717));
		canvas.PushClip(inner);
		const bool pressed = button.GetState() == ButtonState::Pressed;
		canvas.FillGradient(inner, FromArgb(pressed ? 0xFF241C12 : button.enabled ? 0xFF4B361B : 0xFF25221D), FromArgb(0xFF101112), false);
		canvas.PopClip();
		canvas.FillRect(Rect{inner.left + Scale(4), inner.top, inner.right - Scale(4), inner.top + Scale(1)}, FromArgb(0x506D5732));

		if (button.text.empty() || !m_playFont)
		{
			return;
		}

		TextStyle style;
		style.color = button.enabled ? theme::HeroTitleColor : theme::VersionColor;
		style.outline = theme::PlayTextOutline;
		style.outlineWidth = std::max(1, Scale(1));
		style.shadow = theme::TextShadow;
		style.shadowOffset = Point{ 0, Scale(2) };

		// Nudge the label down with the bevel when pressed, so the button reads as
		// physically depressed rather than just recolored.
		Rect textArea = rect;
		if (button.GetState() == ButtonState::Pressed)
		{
			textArea = Offset(textArea, 0, Scale(1));
		}

		DrawText(canvas, *m_playFont, button.text, textArea, TextAlign::Center, style);
	}

	void LauncherView::DrawProgress(Canvas& canvas)
	{
		if (m_progressIndeterminate)
		{
			return;
		}

		const Rect rect = Scale(m_progress.rect);

		// The track art is a soft inner shadow with a transparent center, so it only
		// reads as a recess once something dark sits behind it.
		canvas.FillRoundedRect(rect, Scale(6), theme::ProgressBase);

		if (const Bitmap* track = GetAsset(theme::ProgressTrack.resourceId))
		{
			canvas.DrawNineSlice(*track, Scale(theme::ProgressTrack.insets), rect,
				Color{ 255, 255, 255, 255 }, theme::ProgressTrack.tileEdges);
		}

		const Rect inner = Inflate(rect, -Scale(3));
		if (inner.IsEmpty())
		{
			return;
		}

		Rect fill = inner;
		fill.right = fill.left + static_cast<int32>(inner.GetWidth() * std::clamp(m_progress.value, 0.0f, 1.0f));
		if (fill.GetWidth() <= 0)
		{
			return;
		}

		// No special case for a nearly empty bar: the nine slice clamps its own insets,
		// so the pill degrades to its two rounded caps.
		if (const Bitmap* pill = GetAsset(theme::ProgressFill.resourceId))
		{
			canvas.PushClip(inner);
			canvas.DrawNineSlice(*pill, Scale(theme::ProgressFill.insets), fill,
				theme::ProgressTint, theme::ProgressFill.tileEdges);
			canvas.PopClip();
		}
	}

	void LauncherView::Render(Canvas& canvas)
	{
		// Everything static was composited once; each frame starts from that copy.
		// CopyFrom rather than Blit: the background is partly transparent, and blending
		// it over the previous frame would accumulate instead of replacing.
		if (m_background.IsValid()
			&& m_background.GetWidth() == canvas.GetWidth()
			&& m_background.GetHeight() == canvas.GetHeight())
		{
			canvas.CopyFrom(m_background);
		}
		else
		{
			canvas.Clear(theme::WindowBackground);
		}

		TextStyle titleStyle;
		titleStyle.outline = theme::TitleOutline;
		titleStyle.outlineWidth = 1;
		titleStyle.shadow = theme::TextShadow;
		titleStyle.shadowOffset = Point{ 1, 1 };

		TextStyle bodyStyle;
		bodyStyle.shadow = theme::TextShadow;
		bodyStyle.shadowOffset = Point{ 1, 1 };

		if (m_titleFont)
		{
			DrawLabel(canvas, *m_titleFont, m_titleLabel, titleStyle);
		}

		if (m_versionFont)
		{
			DrawLabel(canvas, *m_versionFont, m_versionLabel, bodyStyle);
		}

		if (m_pageDirty || !m_pageCache.IsValid())
		{
			m_pageCache = Bitmap(canvas.GetWidth(), canvas.GetHeight());
			Canvas pageCanvas(m_pageCache.GetPixels(), canvas.GetWidth(), canvas.GetHeight(), canvas.GetWidth());
			pageCanvas.Clear(theme::Transparent);
			DrawPage(pageCanvas);
			m_pageDirty = false;
		}
		canvas.Blit(m_pageCache, m_pageCache.GetBounds(), canvas.GetBounds(), BlitFilter::Nearest);
		for (size_t i = 0; i < m_navigation.size(); ++i)
		{
			const Button& button = m_navigation[i];
			const bool selected = static_cast<size_t>(m_page) == i;
			if (button.hovered || selected)
			{
				canvas.FillGradient(Scale(button.rect), FromArgb(0x002F2417), FromArgb(0xB02F2417), false);
			}
			DrawTextLine(canvas, button.text, button.rect, false, selected || button.hovered, TextAlign::Center);
			if (selected)
			{
				const int32 center = (button.rect.left + button.rect.right) / 2;
				canvas.FillRoundedRect(Scale(Rect{ center - 38, 93, center + 38, 101 }), Scale(4), FromArgb(0x30FFC04A));
				canvas.FillGradient(Scale(Rect{ center - 36, 96, center, 99 }), FromArgb(0x409D5A16), theme::ProgressTint, true);
				canvas.FillGradient(Scale(Rect{ center, 96, center + 36, 99 }), theme::ProgressTint, FromArgb(0x409D5A16), true);
				canvas.FillRoundedRect(Scale(Rect{ center - 6, 93, center + 6, 99 }), Scale(3), theme::ProgressTint);
				canvas.FillRect(Scale(Rect{ center - 3, 93, center + 3, 95 }), FromArgb(0xFFFFE4A1));
			}
		}
		for (const Button& button : m_pageButtons)
		{
			if (button.hovered && button.enabled)
			{
				canvas.FillRect(Scale(button.rect), FromArgb(0x182F2417));
			}
		}
		const auto controls = GetButtons();
		if (m_focus >= 0 && static_cast<size_t>(m_focus) < controls.size())
		{
			const Rect r = Scale(controls[m_focus]->rect);
			canvas.FillRect(Rect{r.left, r.bottom - Scale(2), r.right, r.bottom}, theme::ProgressTint);
		}

		DrawButton(canvas, m_minimizeButton);
		DrawButton(canvas, m_closeButton);

		if (m_statusFont)
		{
			DrawLabel(canvas, *m_statusFont, m_statusLabel, bodyStyle);
		}

		DrawProgress(canvas);

		if (m_percentFont)
		{
			DrawLabel(canvas, *m_percentFont, m_percentLabel, bodyStyle);
			if (!m_downloadSizeText.empty())
			{
				TextStyle detailStyle = bodyStyle;
				detailStyle.color = theme::StatusColor;
				detailStyle.color = theme::HeroTitleColor;
				DrawText(canvas, *m_percentFont, "TRANSFER", Scale(Rect{664, 609, 834, 628}), TextAlign::Left, detailStyle);
				detailStyle.color = theme::StatusColor;
				DrawText(canvas, *m_percentFont, m_downloadSizeText, Scale(Rect{664, 632, 834, 652}), TextAlign::Left, detailStyle);
				DrawText(canvas, *m_percentFont, m_paused ? "Paused" : m_downloadRateText, Scale(Rect{664, 654, 834, 675}), TextAlign::Left, detailStyle);
			}

			if (!m_noticeLabel.text.empty() && m_downloadSizeText.empty())
			{
				DrawLabel(canvas, *m_percentFont, m_noticeLabel, bodyStyle);
			}
		}

		const Rect pauseRect = Scale(m_pauseButton.rect);
		canvas.FillRoundedRect(pauseRect, Scale(4), FromArgb(m_pauseButton.hovered && m_pauseButton.enabled ? 0xFFB88C3D : 0xFF665331));
		canvas.FillRoundedRect(Scale(Rect{569, 635, 625, 676}), Scale(3), FromArgb(0xFF171717));
		const Color pauseColor = m_pauseButton.enabled ? theme::HeroTitleColor : theme::VersionColor;
		if (m_paused)
		{
			const Rect triangle = Scale(Rect{590, 646, 608, 664});
			const float centerY = (triangle.top + triangle.bottom) * 0.5f;
			for (int32 x = triangle.left; x < triangle.right; ++x)
			{
				const float halfHeight = triangle.GetHeight() * 0.5f *
					(1.0f - (x - triangle.left + 0.5f) / triangle.GetWidth());
				const float top = centerY - halfHeight;
				const float bottom = centerY + halfHeight;
				for (int32 y = static_cast<int32>(std::floor(top)); y < static_cast<int32>(std::ceil(bottom)); ++y)
				{
					const float coverage = std::min(bottom, y + 1.0f) - std::max(top, static_cast<float>(y));
					canvas.FillRect(Rect{x, y, x + 1, y + 1}, ScaleColor(pauseColor, static_cast<uint8>(coverage * 255.0f + 0.5f)));
				}
			}
		}
		else
		{
			canvas.FillRoundedRect(Scale(Rect{589, 647, 594, 663}), Scale(1), pauseColor);
			canvas.FillRoundedRect(Scale(Rect{600, 647, 605, 663}), Scale(1), pauseColor);
		}
		DrawButton(canvas, m_playButton);

		m_dirty = false;
	}
	std::vector<Button*> LauncherView::GetButtons()
	{
		std::vector<Button*> result;
		for (Button& button : m_navigation)
		{
			result.push_back(&button);
		}
		for (Button& button : m_pageButtons)
		{
			result.push_back(&button);
		}
		result.push_back(&m_playButton);
		result.push_back(&m_pauseButton);
		result.push_back(&m_minimizeButton);
		result.push_back(&m_closeButton);
		return result;
	}

	void LauncherView::SelectPage(const Page page)
	{
		m_page = page;
		m_selectedArticle = -1;
		m_scroll = 0;
		m_maxScroll = 0;
		m_focus = -1;
		BuildPageButtons();
		m_dirty = true;
	}

	void LauncherView::BuildPageButtons()
	{
		m_pageDirty = true;
		m_pageButtons.clear();
		m_focus = -1;
		const auto add = [this](const Rect rect, std::function<void()> action, const bool enabled = true)
		{
			Button button;
			button.rect = rect;
			button.onClick = std::move(action);
			button.enabled = enabled;
			m_pageButtons.push_back(std::move(button));
		};
		const auto readNews = [this](const int32 index)
		{
			m_page = Page::News;
			m_selectedArticle = index;
			m_scroll = 0;
			BuildPageButtons();
			m_dirty = true;
		};
		if (m_page == Page::Home)
		{
			add(Rect{64, 422, 384, 578},
				[readNews]
				{
					readNews(0);
				});
			if (m_content.GetNews().size() > 1)
			{
				add(Rect{396, 422, 716, 578},
					[readNews]
					{
						readNews(1);
					});
			}
			add(Rect{748, 526, 1056, 566},
				[this]
				{
					SelectPage(Page::PatchNotes);
				});
		}
		else if (m_page == Page::News)
		{
			if (m_selectedArticle >= 0)
			{
				add(Rect{84, 128, 254, 160},
					[this]
					{
						SelectPage(Page::News);
					});
			}
			else
			{
				for (int32 i = 0; i < 3 && m_newsOffset + i < static_cast<int32>(m_content.GetNews().size()); ++i)
				{
					const Rect rect = i == 0 ? Rect{64, 176, 706, 536} : Rect{724, 176 + (i - 1) * 180, 1076, 344 + (i - 1) * 180};
					add(rect,
						[readNews, index = m_newsOffset + i]
						{
							readNews(index);
						});
				}
				add(
					Rect{738, 540, 850, 574},
					[this]
					{
						m_newsOffset -= 3;
						BuildPageButtons();
						m_dirty = true;
					},
					m_newsOffset >= 3);
				add(
					Rect{950, 540, 1064, 574},
					[this]
					{
						m_newsOffset += 3;
						BuildPageButtons();
						m_dirty = true;
					},
					m_newsOffset + 3 < static_cast<int32>(m_content.GetNews().size()));
			}
		}
		else if (m_page == Page::PatchNotes)
		{
			for (int32 i = 0; i < 5 && m_patchOffset + i < static_cast<int32>(m_content.GetPatches().size()); ++i)
			{
				add(Rect{76, 178 + i * 66, 316, 240 + i * 66},
					[this, index = m_patchOffset + i]
					{
						m_selectedPatch = index;
						m_pageDirty = true;
						m_scroll = 0;
						m_dirty = true;
					});
			}
			add(
				Rect{80, 536, 180, 572},
				[this]
				{
					m_patchOffset -= 5;
					BuildPageButtons();
					m_dirty = true;
				},
				m_patchOffset >= 5);
			add(
				Rect{204, 536, 304, 572},
				[this]
				{
					m_patchOffset += 5;
					BuildPageButtons();
					m_dirty = true;
				},
				m_patchOffset + 5 < static_cast<int32>(m_content.GetPatches().size()));
		}
		else if (m_page == Page::Settings)
		{
			add(Rect{790, 210, 1048, 250},
				[this]
				{
					m_host.OpenGameDirectory();
				});
			add(Rect{96, 280, 356, 322},
				[this]
				{
					if (!m_host.RepairGame())
					{
						m_host.ShowMessage("Repair game files",
										   "Please wait until the current update finishes before checking your files again.");
					}
				});
			add(Rect{96, 420, 1028, 464},
				[this]
				{
					m_keepOpen = !m_keepOpen;
					m_settingsSaved = false;
					m_pageDirty = true;
					m_dirty = true;
				});
			add(Rect{96, 520, 300, 562},
				[this]
				{
					m_host.OpenLogDirectory();
				});
			add(Rect{716, 520, 888, 562},
				[this]
				{
					m_settingsSaved = m_host.SaveKeepOpen(m_keepOpen);
					m_pageDirty = true;
					if (!m_settingsSaved)
					{
						m_host.ShowMessage("Settings",
										   "Could not save launcher settings. Please check write permissions for the game folder.");
					}
					m_dirty = true;
				});
			add(Rect{916, 520, 1048, 562},
				[this]
				{
					m_keepOpen = m_host.GetKeepOpen();
					m_settingsSaved = false;
					m_pageDirty = true;
					m_dirty = true;
				});
		}
		if (m_page == Page::PatchNotes || (m_page == Page::News && m_selectedArticle >= 0))
		{
			add(Rect{1050, 176, 1074, 212},
				[this]
				{
					OnScroll(-100);
				});
			add(Rect{1050, 536, 1074, 574},
				[this]
				{
					OnScroll(100);
				});
		}
	}

	void LauncherView::OnScroll(const int32 delta)
	{
		m_pageDirty = true;
		m_scroll = std::clamp(m_scroll + delta, 0, m_maxScroll);
		m_dirty = true;
	}

	void LauncherView::OnKey(const int32 key, const bool shift)
	{
		if (key == 9)
		{
			const auto buttons = GetButtons();
			const int32 count = static_cast<int32>(buttons.size());
			for (int32 i = 0; i < count; ++i)
			{
				m_focus = (m_focus + (shift ? count - 1 : 1) + count) % count;
				if (buttons[m_focus]->enabled)
				{
					break;
				}
			}
			m_dirty = true;
		}
		else if (key == 13 || key == 32)
		{
			const auto buttons = GetButtons();
			if (m_focus >= 0 && static_cast<size_t>(m_focus) < buttons.size() && buttons[m_focus]->enabled && buttons[m_focus]->onClick)
			{
				// Copy first: page changes rebuild and destroy the originating button.
				auto action = buttons[m_focus]->onClick;
				action();
			}
		}
		else if (key == 38 || key == 33)
		{
			OnScroll(key == 33 ? -300 : -48);
		}
		else if (key == 40 || key == 34)
		{
			OnScroll(key == 34 ? 300 : 48);
		}
		else if (key == 36 || key == 35)
		{
			OnScroll(key == 36 ? -m_maxScroll : m_maxScroll);
		}
	}

	void LauncherView::DrawTextLine(Canvas& canvas, const std::string& text, const Rect area, const bool heading, const bool gold, const TextAlign align)
	{
		FontFace* font = heading ? m_headingFont.get() : m_statusFont.get();
		if (!font)
		{
			return;
		}
		TextStyle style;
		style.color = gold || heading ? theme::HeroTitleColor : theme::StatusColor;
		const Rect physical = Scale(area);
		canvas.PushClip(physical);
		DrawText(canvas, *font, ElideText(*font, text, physical.GetWidth()), physical, align, style);
		canvas.PopClip();
	}

	void LauncherView::DrawPanel(Canvas& canvas, const Rect area)
	{
		// The border must not fill the center: that would make a translucent surface opaque.
		const Color edge = FromArgb(0xBB87704A);
		canvas.FillRect(Scale(Rect{ area.left, area.top, area.right, area.top + 1 }), edge);
		canvas.FillRect(Scale(Rect{ area.left, area.bottom - 1, area.right, area.bottom }), edge);
		canvas.FillRect(Scale(Rect{ area.left, area.top + 1, area.left + 1, area.bottom - 1 }), edge);
		canvas.FillRect(Scale(Rect{ area.right - 1, area.top + 1, area.right, area.bottom - 1 }), edge);
		canvas.FillGradient(Scale(Inflate(area, -1)), FromArgb(0xAA1C2024), FromArgb(0x8806090C), false);
		canvas.FillRect(Scale(Rect{ area.left + 2, area.top + 1, area.right - 2, area.top + 2 }), FromArgb(0x30FFFFFF));
	}

	void LauncherView::DrawArtwork(Canvas& canvas, const uint32 resourceId, const Rect area, const std::string& image)
	{
		const Bitmap* bitmap = GetAsset(resourceId);
		if (m_remoteContent && !image.empty())
		{
			const auto found = m_remoteContent->images.find(image);
			if (found != m_remoteContent->images.end())
			{
				bitmap = found->second.get();
			}
		}
		if (bitmap)
		{
			Rect source = bitmap->GetBounds();
			const double ratio = static_cast<double>(area.GetWidth()) / area.GetHeight();
			if (static_cast<double>(source.GetWidth()) / source.GetHeight() > ratio)
			{
				const int32 width = static_cast<int32>(source.GetHeight() * ratio);
				source.left = (source.GetWidth() - width) / 2;
				source.right = source.left + width;
			}
			else
			{
				const int32 height = static_cast<int32>(source.GetWidth() / ratio);
				source.top = (source.GetHeight() - height) / 2;
				source.bottom = source.top + height;
			}
			canvas.Blit(*bitmap, source, Scale(area));
		}
	}

	int32 LauncherView::DrawParagraph(Canvas& canvas, const std::string& text, const Rect area, const bool heading)
	{
		FontFace* font = heading ? m_headingFont.get() : m_statusFont.get();
		if (!font)
		{
			return 0;
		}
		const int32 lineHeight = heading ? 34 : 23;
		std::istringstream words(text);
		std::string word;
		std::string line;
		int32 y = area.top;
		while (words >> word)
		{
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && MeasureText(*font, candidate) > Scale(area.GetWidth()))
			{
				DrawTextLine(canvas, line, Rect{area.left, y, area.right, y + lineHeight}, heading);
				y += lineHeight;
				line = word;
			}
			else
			{
				line = candidate;
			}
		}
		if (!line.empty())
		{
			DrawTextLine(canvas, line, Rect{area.left, y, area.right, y + lineHeight}, heading);
			y += lineHeight;
		}
		return y - area.top;
	}

	void LauncherView::DrawArticle(Canvas& canvas, const LauncherArticle& article, const Rect area)
	{
		canvas.PushClip(Scale(area));
		int32 y = area.top - m_scroll;
		y += DrawParagraph(canvas, article.title, Rect{area.left, y, area.right - 20, y + 100}, true);
		DrawTextLine(canvas, article.date, Rect{area.left, y, area.right - 20, y + 26});
		y += 42;
		y += DrawParagraph(canvas, article.summary, Rect{area.left, y, area.right - 20, y + 100});
		y += 20;
		if (!article.image.empty())
		{
			DrawArtwork(canvas, IDR_PNG_SPLASH, Rect{area.left, y, area.right - 20, y + 140}, article.image);
			y += 160;
		}
		std::istringstream lines(article.body);
		std::string line;
		while (std::getline(lines, line))
		{
			if (line.empty())
			{
				y += 12;
				continue;
			}
			const bool heading = line.compare(0, 3, "## ") == 0;
			if (heading)
			{
				canvas.FillRect(Scale(Rect{area.left, y + 2, area.right - 20, y + 3}), FromArgb(0xFF413524));
				y += 12;
				line.erase(0, 3);
			}
			y += DrawParagraph(canvas, line, Rect{area.left, y, area.right - 20, y + 100}, heading);
			y += heading ? 8 : 4;
		}
		m_maxScroll = std::max(0, y + m_scroll - area.bottom + 16);
		canvas.PopClip();
		if (m_maxScroll > 0)
		{
			const int32 trackHeight = area.GetHeight();
			const int32 thumbHeight = std::max(28, trackHeight * trackHeight / (trackHeight + m_maxScroll));
			const int32 thumbY = area.top + (trackHeight - thumbHeight) * m_scroll / m_maxScroll;
			canvas.FillRect(Scale(Rect{area.right - 4, area.top, area.right, area.bottom}), FromArgb(0xFF332A1E));
			canvas.FillRoundedRect(Scale(Rect{area.right - 4, thumbY, area.right, thumbY + thumbHeight}), Scale(2), theme::HeroTitleColor);
		}
	}

	void LauncherView::DrawPage(Canvas& canvas)
	{
		const auto& news = m_content.GetNews();
		const auto& patches = m_content.GetPatches();
		if (m_page == Page::Home)
		{
			DrawPanel(canvas, layout::HeroFrame);
			DrawArtwork(canvas, IDR_PNG_SPLASH, layout::HeroImage, m_remoteContent ? m_remoteContent->manifest->heroImage : std::string());
			canvas.FillGradient(Scale(layout::HeroScrim), theme::HeroScrimFrom, theme::HeroScrimTo, false);
			TextStyle heroStyle;
			heroStyle.outline = theme::PlayTextOutline;
			heroStyle.outlineWidth = Scale(2);
			DrawLabel(canvas, *m_heroTitleFont, m_heroTitleLabel, heroStyle);
			DrawTextLine(canvas, m_heroSubtitleLabel.text, layout::HeroSubtitle);
			for (int32 i = 0; i < 2 && i < static_cast<int32>(news.size()); ++i)
			{
				const int32 x = 64 + i * 332;
				DrawPanel(canvas, Rect{x, 422, x + 320, 578});
				DrawArtwork(canvas, i == 0 ? IDR_PNG_NEWS_DEVELOPMENT : IDR_PNG_NEWS_WORLD, Rect{x + 1, 423, x + 319, 577}, news[i].image);
				canvas.FillGradient(Scale(Rect{ x + 1, 478, x + 319, 577 }), FromArgb(0x18080E16), FromArgb(0xEA030609), false);
				canvas.FillGradient(Scale(Rect{ x + 1, 518, x + 319, 577 }), FromArgb(0x7013212B), FromArgb(0xA005080C), false);
				canvas.FillRect(Scale(Rect{ x + 1, 518, x + 319, 519 }), FromArgb(0x20E5F2FF));
				DrawTextLine(canvas, news[i].title, Rect{x + 12, 518, x + 298, 548}, false, true);
				DrawTextLine(canvas, news[i].summary, Rect{x + 12, 546, x + 298, 570});
			}
			DrawPanel(canvas, Rect{732, 112, 1076, 578});
			DrawTextLine(canvas, "LATEST UPDATE", Rect{752, 128, 1056, 168}, true);
			DrawTextLine(canvas, patches.front().title, Rect{752, 170, 1056, 198}, false, true);
			DrawTextLine(canvas, patches.front().date, Rect{752, 198, 1056, 224});
			struct Section
			{
				std::string title;
				std::vector<std::string> lines;
			};
			std::vector<Section> sections;
			std::istringstream lines(patches.front().body);
			std::string line;
			while (std::getline(lines, line))
			{
				if (line.compare(0, 3, "## ") == 0)
				{
					sections.push_back(Section{ line.substr(3), {} });
				}
				else if (!line.empty() && !sections.empty())
				{
					sections.back().lines.push_back(line.compare(0, 2, "- ") == 0 ? line.substr(2) : line);
				}
			}
			canvas.FillRect(Scale(Rect{ 752, 224, 1056, 225 }), FromArgb(0x554C463D));
			canvas.PushClip(Scale(Rect{ 748, 230, 1058, 520 }));
			for (size_t i = 0; i < sections.size() && i < 3; ++i)
			{
				const Section& section = sections[i];
				const int32 y = 238 + static_cast<int32>(i) * 96;
				const Bitmap& icon = m_sectionIcons[i];
				canvas.Blit(icon, icon.GetBounds(), Scale(Rect{ 748, y + 3, 772, y + 27 }), BlitFilter::Nearest);
				TextStyle sectionStyle;
				sectionStyle.color = theme::HeroTitleColor;
				const Rect heading = Scale(Rect{ 786, y, 1056, y + 30 });
				DrawText(canvas, *m_sectionFont, ElideText(*m_sectionFont, section.title, heading.GetWidth()), heading, TextAlign::Left, sectionStyle);
				int32 row = y + 28;
				for (size_t j = 0; j < section.lines.size() && j < 2; ++j)
				{
					std::istringstream words(section.lines[j]);
					std::vector<std::string> wrapped;
					std::string word;
					std::string run;
					while (words >> word)
					{
						const std::string candidate = run.empty() ? word : run + " " + word;
						if (!run.empty() && MeasureText(*m_percentFont, candidate) > Scale(258))
						{
							wrapped.push_back(run);
							run = word;
						}
						else
						{
							run = candidate;
						}
					}
					if (!run.empty())
					{
						wrapped.push_back(run);
					}
					canvas.FillRoundedRect(Scale(Rect{ 786, row + 7, 789, row + 10 }), Scale(1), theme::StatusColor);
					for (size_t k = 0; k < wrapped.size() && k < 2; ++k)
					{
						TextStyle summaryStyle;
						summaryStyle.color = theme::StatusColor;
						const Rect textArea = Scale(Rect{ 798, row, 1056, row + 16 });
						std::string text = wrapped[k];
						if (k == 1 && wrapped.size() > 2)
						{
							text += " ...";
						}
						DrawText(canvas, *m_percentFont, ElideText(*m_percentFont, text, textArea.GetWidth()), textArea, TextAlign::Left, summaryStyle);
						row += 16;
					}
				}
			}
			canvas.PopClip();
			DrawTextLine(canvas, "Read full patch notes  >", Rect{752, 532, 1056, 564}, false, true);
		}
		else if (m_page == Page::News)
		{
			if (m_selectedArticle >= 0)
			{
				DrawPanel(canvas, layout::Page);
				DrawTextLine(canvas, "< Back to news", Rect{84, 128, 254, 160}, false, true);
				DrawArticle(canvas, news[m_selectedArticle], Rect{100, 180, 1040, 558});
				DrawTextLine(canvas, "^", Rect{1052, 180, 1074, 206}, false, true);
				DrawTextLine(canvas, "v", Rect{1052, 540, 1074, 568}, false, true);
			}
			else
			{
				DrawTextLine(canvas, "Latest news", Rect{76, 120, 352, 162}, true);
				DrawTextLine(canvas, "From the world of Alestia", Rect{354, 128, 760, 158});
				for (int32 i = 0; i < 3 && m_newsOffset + i < static_cast<int32>(news.size()); ++i)
				{
					const LauncherArticle& article = news[m_newsOffset + i];
					if (i == 0)
					{
						DrawPanel(canvas, Rect{64, 176, 706, 536});
						DrawArtwork(canvas, IDR_PNG_NEWS_DEVELOPMENT, Rect{65, 177, 705, 535}, article.image);
						canvas.FillGradient(Scale(Rect{65, 310, 705, 535}), theme::HeroScrimFrom, FromArgb(0xFF090807), false);
						DrawTextLine(canvas, article.title, Rect{88, 394, 684, 432}, true);
						DrawTextLine(canvas, article.date, Rect{88, 432, 684, 458});
						DrawTextLine(canvas, article.summary, Rect{88, 458, 684, 486});
						DrawTextLine(canvas, "Read article  >", Rect{88, 492, 684, 524}, false, true);
					}
					else
					{
						const int32 y = 176 + (i - 1) * 180;
						DrawPanel(canvas, Rect{724, y, 1076, y + 168});
						DrawArtwork(canvas, i == 1 ? IDR_PNG_NEWS_WORLD : IDR_PNG_SPLASH, Rect{725, y + 1, 1075, y + 92}, article.image);
						DrawTextLine(canvas, article.title, Rect{738, y + 96, 1064, y + 126}, false, true);
						DrawTextLine(canvas, article.summary, Rect{738, y + 126, 1064, y + 156});
					}
				}
				if (news.size() > 3)
				{
					DrawTextLine(canvas, "< Previous", Rect{738, 540, 850, 574}, false, m_newsOffset >= 3);
					DrawTextLine(canvas, "Next >", Rect{950, 540, 1064, 574}, false, m_newsOffset + 3 < static_cast<int32>(news.size()));
				}
			}
		}
		else if (m_page == Page::PatchNotes)
		{
			DrawPanel(canvas, Rect{64, 112, 330, 578});
			DrawPanel(canvas, Rect{342, 112, 1076, 578});
			DrawTextLine(canvas, "Patch notes", Rect{82, 126, 310, 164}, true);
			for (int32 i = 0; i < 5 && m_patchOffset + i < static_cast<int32>(patches.size()); ++i)
			{
				const int32 index = m_patchOffset + i;
				const int32 y = 178 + i * 66;
				if (index == m_selectedPatch)
				{
					canvas.FillGradient(Scale(Rect{76, y, 316, y + 62}), FromArgb(0xFF46351E), FromArgb(0xFF1B1711), true);
					canvas.FillRect(Scale(Rect{76, y, 79, y + 62}), theme::ProgressTint);
				}
				DrawTextLine(canvas, patches[index].title, Rect{90, y + 4, 304, y + 32}, false, index == m_selectedPatch);
				DrawTextLine(canvas, patches[index].date, Rect{90, y + 32, 304, y + 58});
			}
			if (patches.size() > 5)
			{
				DrawTextLine(canvas, "< Newer", Rect{80, 536, 180, 572}, false, m_patchOffset >= 5);
				DrawTextLine(canvas, "Older >", Rect{204, 536, 304, 572}, false, m_patchOffset + 5 < static_cast<int32>(patches.size()));
			}
			DrawArticle(canvas, patches[m_selectedPatch], Rect{366, 136, 1040, 558});
			DrawTextLine(canvas, "^", Rect{1052, 180, 1074, 206}, false, true);
			DrawTextLine(canvas, "v", Rect{1052, 540, 1074, 568}, false, true);
		}
		else if (m_page == Page::Settings)
		{
			DrawTextLine(canvas, "Settings", Rect{76, 120, 352, 164}, true);
			DrawTextLine(canvas, "Make yourself at home", Rect{354, 128, 760, 158});
			DrawPanel(canvas, Rect{64, 180, 1076, 350});
			DrawTextLine(canvas, "Installation", Rect{96, 188, 670, 224}, true);
			DrawTextLine(canvas, m_host.GetGameDirectory(), Rect{96, 236, 764, 264});
			DrawPanel(canvas, Rect{790, 210, 1048, 250});
			DrawTextLine(canvas, "Open game folder  >", Rect{806, 212, 1032, 248}, false, true);
			DrawPanel(canvas, Rect{96, 280, 356, 322});
			DrawTextLine(canvas, "Check and repair files", Rect{110, 282, 344, 320}, false, true);
			DrawTextLine(canvas, "Rechecks installed files and downloads missing or changed files.", Rect{378, 282, 1048, 320});
			DrawPanel(canvas, Rect{64, 368, 1076, 492});
			DrawTextLine(canvas, "Launcher behavior", Rect{96, 382, 1048, 418}, true);
			DrawPanel(canvas, Rect{98, 430, 118, 450});
			if (m_keepOpen)
			{
				DrawTextLine(canvas, "x", Rect{102, 428, 118, 452}, false, true);
			}
			DrawTextLine(canvas, "Keep the launcher open after starting the game", Rect{136, 422, 1028, 462});
			DrawTextLine(canvas, "Open launcher logs  >", Rect{96, 520, 300, 562}, false, true);
			DrawPanel(canvas, Rect{716, 520, 888, 562});
			DrawTextLine(canvas, m_settingsSaved ? "Saved" : "Save changes", Rect{738, 522, 880, 560}, false, true);
			DrawPanel(canvas, Rect{916, 520, 1048, 562});
			DrawTextLine(canvas, "Cancel", Rect{946, 522, 1038, 560});
		}
	}
}

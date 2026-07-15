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

namespace mmo
{
	namespace
	{
		/// Every image the skin needs. Decoded up front: the whole set is under half a
		/// megabyte and decoding on demand would stutter the first hover.
		constexpr std::array<uint32, 10> RequiredImages = {
			IDR_PNG_SPLASH,
			IDR_PNG_PANEL_BOTTOM,
			IDR_PNG_BORDER_FRAME,
			IDR_PNG_BUTTON_UP,
			IDR_PNG_BUTTON_OVER,
			IDR_PNG_BUTTON_DOWN,
			IDR_PNG_BUTTON_DISABLED,
			IDR_PNG_PROGRESS_TRACK,
			IDR_PNG_PROGRESS_FILL,
			IDR_PNG_ICON_CLOSE
		};

		const NineSliceDef& SelectButtonAsset(const ButtonState state)
		{
			switch (state)
			{
			case ButtonState::Hovered: return theme::ButtonOver;
			case ButtonState::Pressed: return theme::ButtonDown;
			case ButtonState::Disabled: return theme::ButtonDisabled;
			case ButtonState::Normal: break;
			}

			return theme::ButtonUp;
		}

		/// Moves `current` toward `target` at `rate` per second, framerate independently.
		float EaseTowards(const float current, const float target, const float rate, const float deltaSeconds)
		{
			const float t = 1.0f - std::exp(-rate * deltaSeconds);
			return current + (target - current) * t;
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

		m_statusLabel.rect = layout::StatusLabel;
		m_statusLabel.text = "Checking for updates...";
		m_statusLabel.align = TextAlign::Left;
		m_statusLabel.color = theme::StatusColor;

		m_percentLabel.rect = layout::PercentText;
		m_percentLabel.align = TextAlign::Right;
		m_percentLabel.color = theme::PercentColor;

		m_progress.rect = layout::ProgressTrack;

		m_playButton.rect = layout::PlayButton;
		m_playButton.text = "PLAY";
		// Disabled until the update finishes, which makes it the first thing anyone
		// sees; the Disabled art matters as much as the Up art.
		m_playButton.enabled = false;
		m_playButton.onClick = [this] { m_host.LaunchGame(); };

		m_minimizeButton.rect = layout::MinimizeButton;
		m_minimizeButton.iconId = IDR_PNG_ICON_MINIMIZE;
		m_minimizeButton.onClick = [this] { m_host.Minimize(); };

		m_closeButton.rect = layout::CloseButton;
		m_closeButton.iconId = IDR_PNG_ICON_CLOSE;
		m_closeButton.onClick = [this] { m_host.Close(); };

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
		m_playFont = build(display, Scale(theme::PlayFontSize));
		m_versionFont = build(body, Scale(theme::VersionFontSize));
		m_statusFont = build(body, Scale(theme::StatusFontSize));
		m_percentFont = build(body, Scale(theme::PercentFontSize));

		if (!m_titleFont || !m_playFont || !m_versionFont || !m_statusFont || !m_percentFont)
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

		const int32 surfaceWidth = Scale(layout::WindowWidth);
		const int32 surfaceHeight = Scale(layout::WindowHeight);

		// Cover-fit the splash to the window and pre-scale it once, so that drawing it
		// each frame is a straight 1:1 blit.
		if (const Bitmap* splash = GetAsset(IDR_PNG_SPLASH); splash && splash->IsValid())
		{
			const float scaleX = static_cast<float>(surfaceWidth) / static_cast<float>(splash->GetWidth());
			const float scaleY = static_cast<float>(surfaceHeight) / static_cast<float>(splash->GetHeight());
			const float scale = std::max(scaleX, scaleY);

			const int32 width = std::max(1, static_cast<int32>(splash->GetWidth() * scale + 0.5f));
			const int32 height = std::max(1, static_cast<int32>(splash->GetHeight() * scale + 0.5f));

			Bitmap::Resample(*splash, width, height, m_splashScaled);
		}

		const int32 iconSize = Scale(layout::TitleIconSize);
		if (const Bitmap* icon = GetAsset(IDR_PNG_ICON_CLOSE); icon && icon->IsValid())
		{
			Bitmap::Resample(*icon, iconSize, iconSize, m_closeIcon);
		}

		if (const Bitmap* icon = GetAsset(IDR_PNG_ICON_MINIMIZE); icon && icon->IsValid())
		{
			Bitmap::Resample(*icon, iconSize, iconSize, m_minimizeIcon);
		}

		RebuildBackground();
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

		// Splash, cropped around the focal anchor.
		if (m_splashScaled.IsValid())
		{
			const int32 overflowX = m_splashScaled.GetWidth() - surfaceWidth;
			const int32 overflowY = m_splashScaled.GetHeight() - surfaceHeight;
			const int32 offsetX = -static_cast<int32>(overflowX * layout::SplashAnchorX);
			const int32 offsetY = -static_cast<int32>(overflowY * layout::SplashAnchorY);

			canvas.Blit(m_splashScaled, m_splashScaled.GetBounds(),
				Rect{ offsetX, offsetY, offsetX + m_splashScaled.GetWidth(), offsetY + m_splashScaled.GetHeight() },
				BlitFilter::Nearest);
		}

		// Scrims do the real legibility work; the glyph outlines are only insurance
		// for whatever art gets swapped in later.
		canvas.FillGradient(Scale(layout::TopScrim), theme::TopScrimFrom, theme::TopScrimTo, false);
		canvas.FillGradient(Scale(layout::BottomScrim), theme::BottomScrimFrom, theme::BottomScrimTo, false);
		canvas.DrawVignette(Scale(layout::Content), layout::VignetteStrength);

		if (const Bitmap* panel = GetAsset(theme::PanelBottom.resourceId))
		{
			canvas.DrawNineSlice(*panel, Scale(theme::PanelBottom.insets),
				Scale(layout::BottomPanel), Color{ 255, 255, 255, 255 }, theme::PanelBottom.tileEdges);
		}

		canvas.PopClip();

		DrawWindowFrame(canvas);
	}

	void LauncherView::DrawWindowFrame(Canvas& canvas)
	{
		if (const Bitmap* border = GetAsset(theme::WindowBorder.resourceId))
		{
			canvas.DrawNineSlice(*border, Scale(theme::WindowBorder.insets),
				Scale(layout::WindowFrame), Color{ 255, 255, 255, 255 },
				theme::WindowBorder.tileEdges, NineSliceFill::FrameOnly);
		}
	}

	void LauncherView::ApplySnapshot(const UpdateSnapshot& snapshot)
	{
		m_statusLabel.text = snapshot.statusText;
		m_statusLabel.color = snapshot.phase == UpdatePhase::Failed
			? theme::StatusErrorColor
			: theme::StatusColor;

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

	bool LauncherView::Tick(const float deltaSeconds)
	{
		bool moved = false;

		for (Button* button : { &m_playButton, &m_closeButton, &m_minimizeButton })
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
		return !m_closeButton.rect.Contains(logical) && !m_minimizeButton.rect.Contains(logical);
	}

	Button* LauncherView::FindButtonAt(const Point& logical)
	{
		for (Button* button : { &m_playButton, &m_closeButton, &m_minimizeButton })
		{
			if (button->enabled && button->rect.Contains(logical))
			{
				return button;
			}
		}

		return nullptr;
	}

	void LauncherView::OnMouseMove(const Point& logical)
	{
		const Button* hit = FindButtonAt(logical);

		for (Button* button : { &m_playButton, &m_closeButton, &m_minimizeButton })
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
		for (Button* button : { &m_playButton, &m_closeButton, &m_minimizeButton })
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
		for (Button* button : { &m_playButton, &m_closeButton, &m_minimizeButton })
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
				button->onClick();
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

		const NineSliceDef& def = SelectButtonAsset(button.GetState());
		if (const Bitmap* art = GetAsset(def.resourceId))
		{
			canvas.DrawNineSlice(*art, Scale(def.insets), rect, Color{ 255, 255, 255, 255 }, def.tileEdges);
		}

		if (button.text.empty() || !m_playFont)
		{
			return;
		}

		TextStyle style;
		style.color = button.enabled ? theme::PlayTextEnabled : theme::PlayTextDisabled;
		style.outline = theme::PlayTextOutline;
		style.outlineWidth = std::max(1, Scale(2));
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
		}

		DrawButton(canvas, m_playButton);

		m_dirty = false;
	}
}

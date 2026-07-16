// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bitmap.h"
#include "launcher_model.h"
#include "platform_host.h"
#include "text_renderer.h"
#include "widget.h"

#include "base/typedefs.h"
#include "base/non_copyable.h"

#include <memory>
#include <unordered_map>

namespace mmo
{
	class Canvas;

	/// Owns the launcher's widgets, fonts and decoded art, and draws a frame.
	///
	/// Everything here is portable: it is handed a Canvas over whatever buffer the
	/// platform owns and never learns what that buffer is.
	class LauncherView final : public NonCopyable
	{
	public:
		explicit LauncherView(IPlatformHost& host);

		/// Decodes the embedded art and builds the fonts for `dpiScale`.
		/// Returns false if a required asset is missing.
		bool Initialize(float dpiScale);

		/// Rebuilds everything that depends on the scale: fonts, the pre-scaled splash
		/// and the cached background. Cheap enough to call on every DPI change.
		void SetDpiScale(float dpiScale);
		float GetDpiScale() const { return m_dpiScale; }

		/// Applies the latest state from the model.
		void ApplySnapshot(const UpdateSnapshot& snapshot);

		/// Advances animations. Returns true if anything moved and a repaint is needed.
		bool Tick(float deltaSeconds);

		/// Composites a frame. `canvas` must be the physical surface size.
		void Render(Canvas& canvas);

		/// True if the point is in the draggable caption strip, which excludes the
		/// titlebar buttons so that clicking those does not start a window drag.
		bool HitTestCaption(const Point& logical) const;

		void OnMouseMove(const Point& logical);
		void OnMouseLeave();
		void OnMouseDown(const Point& logical);
		void OnMouseUp(const Point& logical);

		/// True if a repaint is pending because state changed outside of Tick.
		bool IsDirty() const { return m_dirty; }

		int32 Scale(int32 logical) const;
		Rect Scale(const Rect& logical) const;
		Insets Scale(const Insets& logical) const;

	private:
		bool LoadAssets();
		bool BuildFonts();

		/// Composites the splash, scrims, vignette and panel into m_background.
		/// Everything above that changes per frame, so this runs only on a scale change.
		void RebuildBackground();

		const Bitmap* GetAsset(uint32 resourceId) const;

		/// Mattes the outer band and draws the ornate window frame over it.
		void DrawWindowFrame(Canvas& canvas);

		void DrawButton(Canvas& canvas, const Button& button);
		void DrawProgress(Canvas& canvas);
		void DrawLabel(Canvas& canvas, FontFace& face, const Label& label,
			const TextStyle& style);

		Button* FindButtonAt(const Point& logical);

		IPlatformHost& m_host;

		float m_dpiScale = 1.0f;
		bool m_dirty = true;

		/// Decoded art, keyed by resource id.
		std::unordered_map<uint32, Bitmap> m_assets;

		/// The feature art cover-fitted to its stage once per scale change, so the
		/// per-frame path is a filtered 1:1 blit rather than a resize.
		Bitmap m_heroScaled;

		/// Titlebar icons resampled once from 128x128 to their drawn size. An 8x
		/// bilinear reduction per frame would alias badly; stb's filtered reduction
		/// does not.
		Bitmap m_closeIcon;
		Bitmap m_minimizeIcon;

		/// Splash + scrims + vignette + panel, composited once.
		Bitmap m_background;

		std::unique_ptr<FontFace> m_titleFont;
		std::unique_ptr<FontFace> m_heroTitleFont;
		std::unique_ptr<FontFace> m_heroSubtitleFont;
		std::unique_ptr<FontFace> m_versionFont;
		std::unique_ptr<FontFace> m_statusFont;
		std::unique_ptr<FontFace> m_percentFont;
		std::unique_ptr<FontFace> m_playFont;

		Button m_playButton;
		Button m_closeButton;
		Button m_minimizeButton;
		ProgressBar m_progress;
		Label m_titleLabel;
		Label m_heroTitleLabel;
		Label m_heroSubtitleLabel;
		Label m_versionLabel;
		Label m_statusLabel;
		Label m_percentLabel;

		bool m_progressIndeterminate = true;
	};
}

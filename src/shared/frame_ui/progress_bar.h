// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame.h"

namespace mmo
{
	/// @brief A horizontal fill bar (health, mana, experience, cast progress, ...).
	///
	/// The bar can optionally animate its fill towards the target value ("SmoothSpeed") and keep
	/// a fading "trail" segment behind a sudden loss ("TrailDelay" / "TrailSpeed"), so damage
	/// reads as a chunk draining away instead of a jump. Both are off by default; themes opt in.
	class ProgressBar : public Frame
	{
	public:
		explicit ProgressBar(const String& type, const String& name);
		~ProgressBar() override;

		void Copy(Frame& frame) override;

		/// @copydoc Frame::Update
		void Update(float elapsed) override;

	public:
		/// @brief Sets the target progress. Animates towards it if smoothing is enabled.
		void SetProgress(float progress);

		/// @brief Sets the progress and skips any fill or trail animation (e.g. on target change).
		void SetProgressInstant(float progress);

		/// @brief Gets the target progress.
		float GetProgress() const { return m_progress; }

		/// @brief Gets the currently displayed (possibly still animating) fill.
		float GetDisplayedProgress() const { return m_displayProgress; }

		/// @brief Gets the currently displayed trail end, which is >= the displayed fill.
		float GetTrailProgress() const { return m_trailProgress; }

		/// @brief Whether the fill or trail is still animating towards the target.
		bool IsAnimating() const;

	protected:
		void PopulateGeometryBuffer() override;

		/// @brief Renders the background ("Enabled" / "Disabled") state imagery.
		void RenderBackground();

		/// @brief Renders the trail and fill using the "Trail" / "Progress" state imagery.
		void RenderFill();

		/// @brief Renders the "Overlay" / "OverlayDisabled" state imagery.
		void RenderOverlay();

		/// @brief Returns the frame rect shrunk by the progress inset (in screen pixels).
		Rect GetFillRect();

	private:
		void OnProgressChanged(const Property& property);
		void OnProgressColorChanged(const Property& property);
		void OnProgressInsetChanged(const Property& property);
		void OnTrailColorChanged(const Property& property);
		void OnSmoothSpeedChanged(const Property& property);
		void OnTrailDelayChanged(const Property& property);
		void OnTrailSpeedChanged(const Property& property);

	protected:
		/// Target progress.
		float m_progress = 0.0f;
		/// Displayed fill, approaches m_progress while smoothing is enabled.
		float m_displayProgress = 0.0f;
		/// Displayed end of the trail behind a loss. Always >= m_displayProgress.
		float m_trailProgress = 0.0f;
		/// Seconds left before the trail starts draining.
		float m_trailHold = 0.0f;
		/// Native UI inset applied before the fill width is scaled by progress.
		float m_progressInset = 0.0f;
		Color m_progressColor = Color::White;
		Color m_trailColor = Color(1.0f, 1.0f, 1.0f, 0.6f);
		/// Exponential approach rate of the fill (1/s). 0 disables smoothing.
		float m_smoothSpeed = 0.0f;
		/// Seconds the trail holds after a loss before it drains. Negative disables the trail.
		float m_trailDelay = -1.0f;
		/// Trail drain speed in bar widths per second.
		float m_trailSpeed = 0.8f;
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "progress_bar.h"
#include "frame_mgr.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>

namespace mmo
{
	namespace
	{
		/// Below this difference an animated value snaps onto its target.
		constexpr float progressSnapEpsilon = 0.0005f;

		float ParseFloat(const Property& property, const float fallback)
		{
			try
			{
				return std::stof(property.GetValue());
			}
			catch (const std::invalid_argument&)
			{
				WLOG("Invalid progress bar number: '" << property.GetValue() << "'");
				return fallback;
			}
			catch (const std::out_of_range&)
			{
				return fallback;
			}
		}

		argb_t ParseArgb(const Property& property)
		{
			argb_t argb = 0;
			std::stringstream colorStream;
			colorStream.str(property.GetValue());
			colorStream.clear();
			colorStream >> std::hex >> argb;
			return argb;
		}
	}

	ProgressBar::ProgressBar(const String& type, const String& name)
		: Frame(type, name)
	{
		m_propConnections += AddProperty("Progress", "0.0").Changed.connect(this, &ProgressBar::OnProgressChanged);
		m_propConnections += AddProperty("ProgressColor", "FFFFFFFF").Changed.connect(this, &ProgressBar::OnProgressColorChanged);
		m_propConnections += AddProperty("ProgressInset", "0").Changed.connect(this, &ProgressBar::OnProgressInsetChanged);
		m_propConnections += AddProperty("TrailColor", "99FFFFFF").Changed.connect(this, &ProgressBar::OnTrailColorChanged);
		m_propConnections += AddProperty("SmoothSpeed", "0").Changed.connect(this, &ProgressBar::OnSmoothSpeedChanged);
		m_propConnections += AddProperty("TrailDelay", "-1").Changed.connect(this, &ProgressBar::OnTrailDelayChanged);
		m_propConnections += AddProperty("TrailSpeed", "0.8").Changed.connect(this, &ProgressBar::OnTrailSpeedChanged);
	}

	ProgressBar::~ProgressBar()
	{
	}

	void ProgressBar::Copy(Frame& frame)
	{
		Frame::Copy(frame);

		// Frame properties are copied automatically, nothing to do here
	}

	void ProgressBar::SetProgress(const float progress)
	{
		if (progress == m_progress)
		{
			return;
		}

		const float previous = m_progress;
		m_progress = progress;

		// A hidden bar never ticks, so it must not start an animation it would resume later.
		if (!IsVisible())
		{
			SetProgressInstant(progress);
			return;
		}

		if (m_smoothSpeed <= 0.0f)
		{
			m_displayProgress = progress;
		}

		if (m_trailDelay < 0.0f)
		{
			// No trail: it always ends where the visible fill does.
			m_trailProgress = m_displayProgress;
		}
		else if (progress < previous)
		{
			// Keep the trail where the visible fill was, and restart the hold on every new loss.
			m_trailProgress = std::max(m_trailProgress, std::max(previous, m_displayProgress));
			m_trailHold = m_trailDelay;
		}
		else if (m_trailProgress < progress)
		{
			// A gain: the trail runs ahead of a smoothed fill as a short preview of the new value.
			m_trailProgress = progress;
		}

		Invalidate();
	}

	void ProgressBar::SetProgressInstant(const float progress)
	{
		m_progress = progress;
		m_displayProgress = progress;
		m_trailProgress = progress;
		m_trailHold = 0.0f;
		Invalidate();
	}

	bool ProgressBar::IsAnimating() const
	{
		return std::fabs(m_displayProgress - m_progress) > progressSnapEpsilon ||
			m_trailProgress > m_displayProgress + progressSnapEpsilon;
	}

	void ProgressBar::Update(const float elapsed)
	{
		Frame::Update(elapsed);

		if (!IsAnimating())
		{
			return;
		}

		// Fill: frame-rate independent exponential approach, snapping when close.
		if (m_smoothSpeed > 0.0f)
		{
			const float t = 1.0f - std::exp(-m_smoothSpeed * elapsed);
			m_displayProgress += (m_progress - m_displayProgress) * t;
			if (std::fabs(m_displayProgress - m_progress) <= progressSnapEpsilon)
			{
				m_displayProgress = m_progress;
			}
		}
		else
		{
			m_displayProgress = m_progress;
		}

		// Trail: hold, then drain linearly down onto the fill.
		if (m_trailDelay < 0.0f)
		{
			m_trailProgress = m_displayProgress;
		}
		else if (m_trailProgress > m_displayProgress)
		{
			if (m_trailHold > 0.0f)
			{
				m_trailHold -= elapsed;
			}
			else
			{
				m_trailProgress = std::max(m_displayProgress, m_trailProgress - m_trailSpeed * elapsed);
			}
		}

		if (m_trailProgress < m_displayProgress)
		{
			m_trailProgress = m_displayProgress;
		}

		Invalidate(false);
	}

	Rect ProgressBar::GetFillRect()
	{
		// Inset before scaling, so small fills never acquire a negative
		// width or extend outside their housing. Zero preserves Legacy.
		Rect frameRect = GetAbsoluteFrameRect();
		const auto scale = FrameManager::Get().GetUIScale();
		frameRect.left += m_progressInset * scale.x;
		frameRect.right -= m_progressInset * scale.x;
		frameRect.top += m_progressInset * scale.y;
		frameRect.bottom -= m_progressInset * scale.y;
		return frameRect;
	}

	void ProgressBar::RenderBackground()
	{
		const auto* imagery = GetStateImageryByName(IsEnabled() ? "Enabled" : "Disabled");
		if (!imagery)
		{
			imagery = GetStateImageryByName("Enabled");
		}

		if (imagery)
		{
			imagery->Render(GetAbsoluteFrameRect(), m_color);
		}
	}

	void ProgressBar::RenderFill()
	{
		const auto* progressImagery = GetStateImageryByName("Progress");
		if (!progressImagery)
		{
			return;
		}

		const Rect fillRect = GetFillRect();
		if (fillRect.GetWidth() <= 0.0f || fillRect.GetHeight() <= 0.0f)
		{
			return;
		}

		// Trail behind a loss, drawn first so the fill covers its left part.
		if (m_trailProgress > m_displayProgress && m_trailProgress > 0.0f)
		{
			const auto* trailImagery = GetStateImageryByName("Trail");
			Rect trailRect = fillRect;
			trailRect.SetWidth(fillRect.GetWidth() * std::clamp(m_trailProgress, 0.0f, 1.0f));
			(trailImagery ? trailImagery : progressImagery)->Render(trailRect, m_trailColor);
		}

		if (m_displayProgress > 0.0f)
		{
			Rect rect = fillRect;
			rect.SetWidth(fillRect.GetWidth() * std::min(1.0f, m_displayProgress));
			progressImagery->Render(rect, m_progressColor);
		}
	}

	void ProgressBar::RenderOverlay()
	{
		const auto* overlayImagery = GetStateImageryByName(IsEnabled() ? "Overlay" : "OverlayDisabled");
		if (overlayImagery)
		{
			overlayImagery->Render(GetAbsoluteFrameRect(), m_color);
		}
	}

	void ProgressBar::PopulateGeometryBuffer()
	{
		Frame::PopulateGeometryBuffer();

		RenderBackground();
		RenderFill();
		RenderOverlay();
	}

	void ProgressBar::OnProgressChanged(const Property& property)
	{
		SetProgress(ParseFloat(property, 0.0f));
	}

	void ProgressBar::OnProgressColorChanged(const Property& property)
	{
		m_progressColor = ParseArgb(property);
		Invalidate(false);
	}

	void ProgressBar::OnTrailColorChanged(const Property& property)
	{
		m_trailColor = ParseArgb(property);
		Invalidate(false);
	}

	void ProgressBar::OnProgressInsetChanged(const Property& property)
	{
		m_progressInset = std::max(0.0f, ParseFloat(property, 0.0f));
		Invalidate(false);
	}

	void ProgressBar::OnSmoothSpeedChanged(const Property& property)
	{
		m_smoothSpeed = std::max(0.0f, ParseFloat(property, 0.0f));

		// Changing the animation setup never leaves a stale animation running.
		SetProgressInstant(m_progress);
	}

	void ProgressBar::OnTrailDelayChanged(const Property& property)
	{
		m_trailDelay = ParseFloat(property, -1.0f);
		SetProgressInstant(m_progress);
	}

	void ProgressBar::OnTrailSpeedChanged(const Property& property)
	{
		m_trailSpeed = std::max(0.01f, ParseFloat(property, 0.8f));
	}
}
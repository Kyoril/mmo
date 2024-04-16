
#include "progress_bar.h"

#include <string>
#include <stdexcept>

namespace mmo
{
	ProgressBar::ProgressBar(const String& type, const String& name)
		: Frame(type, name)
	{
		m_propConnections += AddProperty("Progress", "0.0").Changed.connect(this, &ProgressBar::OnProgressChanged);
	}

	ProgressBar::~ProgressBar()
	{
	}

	void ProgressBar::Copy(Frame& frame)
	{
		Frame::Copy(frame);

	}

	void ProgressBar::SetProgress(const float progress)
	{
		if (progress == m_progress)
		{
			return;
		}

		m_progress =  progress;
		Invalidate();
	}

	void ProgressBar::PopulateGeometryBuffer()
	{
		Frame::PopulateGeometryBuffer();

		// Detect the current state
		std::string activeState = "Disabled";
		if (IsEnabled())
		{
			activeState = "Enabled";
		}

		// Find the state imagery
		const auto* imagery = GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = GetStateImageryByName("Enabled");
		}

		// If found, draw the state imagery
		Rect frameRect = GetAbsoluteFrameRect();
		if (imagery)
		{
			imagery->Render(frameRect, Color::White);
		}

		// If there is no progress, we don't need to render anything
		if (m_progress > 0.0f)
		{
			// Find the progress imagery
			const auto* progressImagery = GetStateImageryByName("Progress");
			if (progressImagery)
			{
				// Adjust the width
				frameRect.SetWidth(frameRect.GetWidth() * std::min(1.0f, m_progress));
				progressImagery->Render(frameRect, Color::White);
			}
		}

		// Find the state imagery
		const auto* overlayImagery = GetStateImageryByName(IsEnabled() ? "Overlay" : "OverlayDisabled");
		if (overlayImagery)
		{
			overlayImagery->Render(GetAbsoluteFrameRect(), Color::White);
		}
	}

	void ProgressBar::OnProgressChanged(const Property& property)
	{
		try
		{
			SetProgress(std::stof(property.GetValue()));
		}
		catch(const std::invalid_argument&)
		{
			WLOG("Invalid argument for progress bar progress: '" + property.GetValue() << "'");
			SetProgress(0.0f);
		}
		
	}
}

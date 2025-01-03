
#include "thumb.h"

#include "frame_mgr.h"

namespace mmo
{
	Thumb::Thumb(const std::string& type, const std::string& name)
		: Button(type, name)
	{
	}

	void Thumb::OnThumbPositionChanged()
	{
		thumbPositionChanged(*this);
	}

	void Thumb::OnThumbTrackStarted()
	{
		thumbPositionChanged(*this);
	}

	void Thumb::OnThumbTrackEnded()
	{
		thumbPositionChanged(*this);
	}

	void Thumb::OnMouseDown(MouseButton button, int32 buttons, const Point& position)
	{
		Button::OnMouseDown(button, buttons, position);

		if (button == MouseButton::Left)
		{
			m_dragged = true;
			m_dragPoint = position;
			OnThumbTrackStarted();
		}
	}

	void Thumb::OnMouseUp(MouseButton button, int32 buttons, const Point& position)
	{
		Button::OnMouseUp(button, buttons, position);

		if (button == MouseButton::Left)
		{
			m_dragged = false;
			OnThumbTrackEnded();

			OnThumbPositionChanged();
		}
	}

	void Thumb::OnMouseMoved(const Point& position, const Point& delta)
	{
		Button::OnMouseMoved(position, delta);

		if (!m_dragged)
		{
			return;
		}

		// Inverse scale
		const float scaleY = 1.0f / FrameManager::Get().GetUIScale().y;

		// Calculate the new position of the thumb
		if (m_verticalMovement && std::abs(delta.y) > 0.0f)
		{
			// Limit thumb position
			if (!AnchorsSatisfyYPosition())
			{
				auto newPosition = Point(GetPosition().x, GetPosition().y + delta.y * scaleY);

				if (newPosition.y < m_vertMin) newPosition.y = m_vertMin;
				if (newPosition.y > m_vertMax) newPosition.y = m_vertMax;

				SetPosition(newPosition);
			}
			else
			{
				// Thumb is anchored
				auto anchorIt = m_anchors.find(anchor_point::Top);
				if (anchorIt != m_anchors.end())
				{
					anchorIt->second->SetOffset(anchorIt->second->GetOffset() + delta.y * scaleY);
				}
				else
				{
					anchorIt = m_anchors.find(anchor_point::Bottom);
					if (anchorIt != m_anchors.end())
					{
						anchorIt->second->SetOffset(anchorIt->second->GetOffset() + delta.y * scaleY);
					}
					else
					{
						anchorIt = m_anchors.find(anchor_point::VerticalCenter);
						if (anchorIt != m_anchors.end())
						{
							anchorIt->second->SetOffset(anchorIt->second->GetOffset() + delta.y * scaleY);
						}
						else
						{
							UNREACHABLE();
						}
					}
				}

				m_needsRedraw = true;
				m_needsLayout = true;
			}

			if (m_realTimeTracking)
			{
				OnThumbPositionChanged();
			}
		}
	}
}

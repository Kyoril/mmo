
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
		const float scaleX = 1.0f / FrameManager::Get().GetUIScale().x;

		// Calculate the new position of the thumb
		if (m_verticalMovement && std::abs(delta.y) > 0.0f)
		{
			// Limit thumb position
			auto newPosition = Point(GetPosition().x, GetPosition().y + delta.y * scaleY);

			// Ensure thumb stays within vertical bounds
			if (newPosition.y < m_vertMin)
			{
				newPosition.y = m_vertMin;
			}
			else if (newPosition.y > m_vertMax - GetHeight())
			{
				newPosition.y = m_vertMax - GetHeight();
			}

			SetPosition(newPosition);

			if (m_realTimeTracking)
			{
				OnThumbPositionChanged();
			}
		}
		
		// Handle horizontal movement if enabled
		if (m_horizontalMovement && std::abs(delta.x) > 0.0f)
		{
			// Limit thumb position
			if (!AnchorsSatisfyXPosition())
			{
				auto newPosition = Point(GetPosition().x + delta.x * scaleX, GetPosition().y);

				// Ensure thumb stays within horizontal bounds
				if (newPosition.x < m_horizontalMin) 
				{
					newPosition.x = m_horizontalMin;
				}
				else if (newPosition.x > m_horizontalMax - GetWidth()) 
				{
					newPosition.x = m_horizontalMax - GetWidth();
				}

				SetPosition(newPosition);
			}
			else
			{
				// Handle anchored horizontal movement with bounds checking
				auto anchorIt = m_anchors.find(anchor_point::Left);
				if (anchorIt != m_anchors.end())
				{
					float newOffset = anchorIt->second->GetOffset() + delta.x * scaleX;
					
					// Ensure thumb stays within horizontal bounds
					if (GetX() + newOffset < m_horizontalMin)
					{
						newOffset = m_horizontalMin - GetX();
					}
					else if (GetX() + newOffset > m_horizontalMax - GetWidth())
					{
						newOffset = m_horizontalMax - GetWidth() - GetX();
					}
					
					anchorIt->second->SetOffset(newOffset);
				}
				else
				{
					anchorIt = m_anchors.find(anchor_point::Right);
					if (anchorIt != m_anchors.end())
					{
						float newOffset = anchorIt->second->GetOffset() + delta.x * scaleX;
						
						// Ensure thumb stays within horizontal bounds
						if (GetX() + GetWidth() + newOffset > m_horizontalMax)
						{
							newOffset = m_horizontalMax - GetX() - GetWidth();
						}
						else if (GetX() + newOffset < m_horizontalMin)
						{
							newOffset = m_horizontalMin - GetX();
						}
						
						anchorIt->second->SetOffset(newOffset);
					}
					else
					{
						anchorIt = m_anchors.find(anchor_point::HorizontalCenter);
						if (anchorIt != m_anchors.end())
						{
							float newOffset = anchorIt->second->GetOffset() + delta.x * scaleX;
							
							// Ensure thumb stays within horizontal bounds
							float halfWidth = GetWidth() / 2.0f;
							if (GetX() + halfWidth + newOffset > m_horizontalMax)
							{
								newOffset = m_horizontalMax - GetX() - halfWidth;
							}
							else if (GetX() - halfWidth + newOffset < m_horizontalMin)
							{
								newOffset = m_horizontalMin - GetX() + halfWidth;
							}
							
							anchorIt->second->SetOffset(newOffset);
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

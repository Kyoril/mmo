#pragma once

#include "button.h"

namespace mmo
{
	class Thumb : public Button
	{
	public:
		signal<void(Thumb&)> thumbPositionChanged;

		signal<void(Thumb&)> thumbTrackStarted;

		signal<void(Thumb&)> thumbTrackEnded;

	public:
		Thumb(const std::string& type, const std::string& name);
		virtual ~Thumb() override = default;

		void SetVerticalRange(float min, float max) { ASSERT(max > min); m_vertMin = min; m_vertMax = max; }

		void SetHorizontalRange(float min, float max) { ASSERT(max > min); m_horizontalMin = min; m_horizontalMax = max; }

		void SetVerticalMovement(const bool verticalMovement) { m_verticalMovement = verticalMovement; }

		void SetHorizontalMovement(const bool horizontalMovement) { m_horizontalMovement = horizontalMovement; }

		[[nodiscard]] float GetVerticalMin() const { return m_vertMin; }

		[[nodiscard]] float GetVerticalMax() const { return m_vertMax; }

		[[nodiscard]] float GetHorizontalMin() const { return m_horizontalMin; }

		[[nodiscard]] float GetHorizontalMax() const { return m_horizontalMax; }

		[[nodiscard]] bool IsVerticalMovement() const { return m_verticalMovement; }

		[[nodiscard]] bool IsHorizontalMovement() const { return m_horizontalMovement; }

		[[nodiscard]] bool IsRealTimeTracking() const { return m_realTimeTracking; }

	protected:
		virtual void OnThumbPositionChanged();

		virtual void OnThumbTrackStarted();

		virtual void OnThumbTrackEnded();

	protected:
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position) override;

		virtual void OnMouseMoved(const Point& position, const Point& delta) override;

	protected:
		bool m_verticalMovement = true;
		bool m_horizontalMovement = false;
		bool m_realTimeTracking = true;

		float m_vertMin = 0.0f;
		float m_vertMax = 0.0f;
		float m_horizontalMin = 0.0f;
		float m_horizontalMax = 0.0f;
		bool m_dragged = false;
		Point m_dragPoint {};
	};
}

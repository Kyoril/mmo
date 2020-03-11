// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "rect.h"

#include "base/macros.h"
#include "base/non_copyable.h"

#include <string>


namespace mmo
{
	class Frame;


	/// Enumerates relative anchor points for frames and frame elements.
	namespace anchor_point
	{
		enum Type
		{
			// No anchor point
			None = 0x00,

			// Edges
			Top,
			Right,
			Bottom,
			Left,

			// Center
			HorizontalCenter,
			VerticalCenter,
		};
	}

	typedef anchor_point::Type AnchorPoint;

	/// Returns the name of a given anchor point enum.
	std::string AnchorPointName(AnchorPoint point);
	/// Returns an anchor point enum value by name.
	/// @returns anchor_point::None in case of an error or invalid name.
	AnchorPoint AnchorPointByName(const std::string& name);

	/// Gets the opposite anchor point.
	inline AnchorPoint OppositeAnchorPoint(AnchorPoint point)
	{
		switch (point)
		{
		case anchor_point::Left: return anchor_point::Right;
		case anchor_point::Top: return anchor_point::Bottom;
		case anchor_point::Right: return anchor_point::Left;
		case anchor_point::Bottom: return anchor_point::Top;
		default:
			return anchor_point::None;
		}
	}


	/// This class handles anchor points.
	class Anchor final
		: public NonCopyable
	{
	public:
		Anchor(AnchorPoint point, AnchorPoint relativePoint, std::weak_ptr<Frame> relativeTo, float offset)
			: m_point(point)
			, m_relativePoint(relativePoint)
			, m_offset(offset)
			, m_relativeTo(std::move(relativeTo))
		{
			ASSERT(m_point != AnchorPoint::None);
			ASSERT(m_relativePoint != AnchorPoint::None);
		}

	public:
		/// Applies this anchor point's logic to an absolute rect that is passed.
		void ApplyToAbsRect(Rect& rect, const Rect& parentRect, bool hasOppositeAnchor);
		/// 
		float GetValueByPoint(const Rect& absRect, AnchorPoint point);

	public:
		inline AnchorPoint GetPoint() const { return m_point; }
		inline AnchorPoint GetRelativePoint() const { return m_relativePoint; }
		inline Frame* GetRelativeTo() const { return m_relativeTo.lock().get(); }
		inline float GetOffset() const { return m_offset; }

	private:
		AnchorPoint m_point;
		AnchorPoint m_relativePoint;
		float m_offset;
		std::weak_ptr<Frame> m_relativeTo;
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <map>
#include <vector>

namespace mmo
{
	/// A plaza nameplate on screen, before decluttering. Screen y grows downwards, so "up" is a
	/// smaller y. The plate covers [bottom - height, bottom] vertically and width around x.
	struct PlazaNameplateBox
	{
		/// Stable identity across frames (the character's order index), for the easing.
		int32 key = -1;

		/// Horizontal centre.
		float x = 0.0f;

		/// Bottom edge where the plate's anchor puts it.
		float bottom = 0.0f;

		float width = 0.0f;
		float height = 0.0f;

		/// Distance of the character from the camera: nearer plates keep their place, farther
		/// ones make way.
		float depth = 0.0f;

		/// The selected character's plate: placed first, so it keeps its place if it can.
		bool priority = false;

		/// Extra clearance this plate needs before it counts as free of the others. The declutter
		/// sets it for plates that made way last frame, so they only return to their anchors once
		/// clearly free and do not flip back and forth.
		float hysteresis = 0.0f;
	};

	/// Resolves overlapping plates by moving them vertically. Plates are placed one after another,
	/// the priority plate first, then by depth (nearest first, ties by key); each plate keeps its
	/// anchor if it overlaps none of the plates placed before it, otherwise it moves up just above
	/// them. A plate that would leave the screen at the top moves down below them instead, and is
	/// clamped to the screen if that does not fit either.
	/// @param boxes The plates.
	/// @param gap Space kept between plates.
	/// @param screenTop Smallest y a plate's top may have.
	/// @param screenBottom Largest y a plate's bottom may have.
	/// @return For each box (same order), the offset to add to its bottom.
	[[nodiscard]] std::vector<float> ResolvePlazaNameplateOffsets(const std::vector<PlazaNameplateBox>& boxes, float gap, float screenTop, float screenBottom);

	/// Declutters the plaza nameplates every frame and eases each plate towards its resolved
	/// offset, so plates glide apart instead of jumping when characters move.
	class PlazaNameplateDeclutter final
	{
	public:
		/// How fast a plate follows its resolved offset (1 / seconds, exponential).
		static constexpr float EaseRate = 12.0f;

		/// Extra clearance a plate that made way needs before it returns to its anchor.
		static constexpr float Hysteresis = 16.0f;

	public:
		/// Resolves the plates and advances the easing. A plate seen for the first time starts at
		/// its resolved offset; plates no longer passed are forgotten.
		/// @param boxes The plates.
		/// @param deltaSeconds Time since the last update.
		/// @param gap Space kept between plates.
		/// @param screenTop Smallest y a plate's top may have.
		/// @param screenBottom Largest y a plate's bottom may have.
		/// @return For each box (same order), the eased offset to add to its bottom.
		const std::vector<float>& Update(const std::vector<PlazaNameplateBox>& boxes, float deltaSeconds, float gap, float screenTop, float screenBottom);

		/// Forgets every plate, so the next update places them without easing.
		void Reset()
		{
			m_offsets.clear();
			m_moved.clear();
		}

	private:
		std::map<int32, float> m_offsets;

		/// Whether each plate (by key) had to make way at the last update.
		std::map<int32, bool> m_moved;
		std::vector<float> m_result;
	};
}

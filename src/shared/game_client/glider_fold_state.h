// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// @brief Edges of the glider's visible cycle, reported by GliderFoldState::Update.
	namespace glider_event
	{
		enum Type
		{
			/// Nothing changed visibly this update.
			None,
			/// The glider became visible and starts to unfold.
			Opened,
			/// The glider finished folding and is hidden again.
			Closed
		};
	}

	/// @brief Unfold animation of the glider: moves towards open while the unit glides and
	///	towards folded otherwise, at a constant rate over FoldSeconds. Pure logic, no scene
	///	objects, so it can be tested without a graphics device.
	class GliderFoldState final
	{
	public:
		/// Time in seconds to fully unfold or fold the glider.
		static constexpr float FoldSeconds = 0.25f;

	public:
		/// @brief Advances the unfold towards open (gliding) or folded.
		/// @param gliding Whether the unit glides this frame.
		/// @param deltaTime Frame time in seconds (clamped to the unfold range).
		/// @return Opened when a hidden glider starts to open, Closed when a folding glider
		///	becomes hidden, None otherwise. Re-opening a glider that is still folding reverses
		///	it without a second Opened.
		glider_event::Type Update(bool gliding, float deltaTime);

		/// @brief Gets the unfold fraction: 0 = folded, 1 = fully open.
		[[nodiscard]] float GetUnfold() const { return m_unfold; }

		/// @brief Whether the glider is shown (between Opened and Closed).
		[[nodiscard]] bool IsVisible() const { return m_visible; }

	private:
		float m_unfold{0.0f};
		bool m_visible{false};
	};
}

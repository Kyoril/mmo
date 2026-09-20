// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "scene_graph/environment_profile.h"
#include "scene_graph/environment_state.h"

#include <memory>
#include <vector>

namespace mmo
{
	/// @brief Blends environment profiles over time and produces the state for the current hour.
	/// @remark Pure logic: no graphics device, no signals, no resource managers. Main thread only.
	///         A new target fades in over its own transitionSeconds while every other profile fades
	///         out proportionally, so the weights always sum to one. Every weighted profile is
	///         evaluated at the current time, so the day keeps moving during a fade.
	class EnvironmentController final : public NonCopyable
	{
	public:
		/// @brief Most profiles blended at once. A fifth drops the lowest-weight one.
		static constexpr size_t MaxBlendEntries = 4;

	public:
		/// @brief Starts fully on EnvironmentProfile::GetDefault() at noon.
		EnvironmentController();

		/// @brief Chooses the profile to fade to.
		/// @param profile The profile. Null means the built-in Default.
		/// @param immediate Snap without fading (world enter, teleport, editor selection). A
		///        profile with transitionSeconds <= 0 always snaps.
		void SetTarget(std::shared_ptr<const EnvironmentProfile> profile, bool immediate);

		/// @brief Advances the fade and re-evaluates the state.
		/// @param deltaSeconds Real time since the last update.
		/// @param normalizedTime The shared clock, 0 = midnight, 0.5 = noon.
		void Update(float deltaSeconds, float normalizedTime);

		/// @brief The state produced by the last Update or SetTarget.
		[[nodiscard]] const EnvironmentState& GetState() const { return m_state; }

		/// @brief Number of profiles currently blended.
		[[nodiscard]] size_t GetBlendEntryCount() const { return m_entries.size(); }

		/// @brief Current blend weight of a profile, 0 if it is not blended.
		[[nodiscard]] float GetWeight(const EnvironmentProfile* profile) const;

	private:
		struct Entry
		{
			std::shared_ptr<const EnvironmentProfile> profile;
			float weight = 0.0f;
		};

		/// @brief Rescales every weight so the sum is one.
		void Normalize();

		/// @brief Evaluates and combines every entry at m_normalizedTime.
		void Evaluate();

	private:
		/// @brief Blended profiles. The last entry is always the target.
		std::vector<Entry> m_entries;
		float m_normalizedTime = 0.5f;
		EnvironmentState m_state;
	};
}

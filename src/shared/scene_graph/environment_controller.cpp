// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_controller.h"

#include <algorithm>

namespace mmo
{
	namespace
	{
		/// Weights below this are dropped so finished fades stop costing curve evaluations.
		constexpr float MinWeight = 1.0e-4f;
	}

	EnvironmentController::EnvironmentController()
	{
		m_entries.push_back({ EnvironmentProfile::GetDefault(), 1.0f });
		Evaluate();
	}

	void EnvironmentController::SetTarget(std::shared_ptr<const EnvironmentProfile> profile, const bool immediate)
	{
		if (!profile)
		{
			profile = EnvironmentProfile::GetDefault();
		}

		if (immediate || profile->transitionSeconds <= 0.0f)
		{
			m_entries.clear();
			m_entries.push_back({ std::move(profile), 1.0f });
			Evaluate();
			return;
		}

		if (m_entries.back().profile == profile)
		{
			return;
		}

		// A profile that is still fading out keeps its weight and becomes the target again.
		const auto existing = std::find_if(m_entries.begin(), m_entries.end(),
			[&profile](const Entry& entry) { return entry.profile == profile; });

		Entry target{ std::move(profile), 0.0f };
		if (existing != m_entries.end())
		{
			target.weight = existing->weight;
			m_entries.erase(existing);
		}

		m_entries.push_back(std::move(target));

		if (m_entries.size() > MaxBlendEntries)
		{
			// Never drop the target (last entry).
			const auto lowest = std::min_element(m_entries.begin(), m_entries.end() - 1,
				[](const Entry& a, const Entry& b) { return a.weight < b.weight; });
			m_entries.erase(lowest);
			Normalize();
		}
	}

	void EnvironmentController::Update(const float deltaSeconds, const float normalizedTime)
	{
		m_normalizedTime = normalizedTime;

		Entry& target = m_entries.back();
		if (target.weight < 1.0f)
		{
			const float duration = target.profile->transitionSeconds;
			const float newWeight = duration > 0.0f ? std::min(1.0f, target.weight + deltaSeconds / duration) : 1.0f;

			// Shrink every other entry by the same factor so the sum stays one.
			const float othersBefore = 1.0f - target.weight;
			const float othersAfter = 1.0f - newWeight;
			const float scale = othersBefore > 0.0f ? othersAfter / othersBefore : 0.0f;
			for (size_t i = 0; i + 1 < m_entries.size(); ++i)
			{
				m_entries[i].weight *= scale;
			}
			target.weight = newWeight;

			m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end() - 1,
				[](const Entry& entry) { return entry.weight < MinWeight; }), m_entries.end() - 1);

			if (m_entries.size() == 1)
			{
				m_entries.back().weight = 1.0f;
			}
		}

		Evaluate();
	}

	float EnvironmentController::GetWeight(const EnvironmentProfile* profile) const
	{
		for (const Entry& entry : m_entries)
		{
			if (entry.profile.get() == profile)
			{
				return entry.weight;
			}
		}

		return 0.0f;
	}

	void EnvironmentController::Normalize()
	{
		float sum = 0.0f;
		for (const Entry& entry : m_entries)
		{
			sum += entry.weight;
		}

		if (sum <= 0.0f)
		{
			// Only possible when every remaining entry had weight 0: give it all to the target.
			for (Entry& entry : m_entries)
			{
				entry.weight = 0.0f;
			}
			m_entries.back().weight = 1.0f;
			return;
		}

		for (Entry& entry : m_entries)
		{
			entry.weight /= sum;
		}
	}

	void EnvironmentController::Evaluate()
	{
		// Running blend: after folding in entry i the accumulated state is the weighted average of
		// entries 0..i, so the fold weight is w_i / (w_0 + ... + w_i).
		float accumulated = 0.0f;
		bool first = true;

		for (const Entry& entry : m_entries)
		{
			if (entry.weight <= 0.0f)
			{
				continue;
			}

			const EnvironmentState state = EvaluateEnvironment(*entry.profile, m_normalizedTime);
			accumulated += entry.weight;

			if (first)
			{
				m_state = state;
				first = false;
			}
			else
			{
				m_state = LerpEnvironment(m_state, state, entry.weight / accumulated);
			}
		}

		if (first)
		{
			m_state = EvaluateEnvironment(*m_entries.back().profile, m_normalizedTime);
		}
	}
}

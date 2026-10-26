// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "plaza_nameplate_layout.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mmo
{
	namespace
	{
		struct PlacedPlate
		{
			float x;
			float halfWidth;
			float top;
			float bottom;
		};

		bool Overlaps(const PlacedPlate& placed, const float x, const float halfWidth, const float top, const float bottom, const float gap, const float extra)
		{
			if (std::abs(placed.x - x) >= placed.halfWidth + halfWidth + extra)
			{
				return false;
			}

			return top < placed.bottom + gap + extra && placed.top < bottom + gap + extra;
		}
	}

	std::vector<float> ResolvePlazaNameplateOffsets(const std::vector<PlazaNameplateBox>& boxes, const float gap, const float screenTop, const float screenBottom)
	{
		std::vector<float> offsets(boxes.size(), 0.0f);

		std::vector<size_t> order(boxes.size());
		std::iota(order.begin(), order.end(), size_t{ 0 });
		std::stable_sort(order.begin(), order.end(), [&boxes](const size_t a, const size_t b)
			{
				const PlazaNameplateBox& first = boxes[a];
				const PlazaNameplateBox& second = boxes[b];
				if (first.priority != second.priority)
				{
					return first.priority;
				}

				if (first.depth != second.depth)
				{
					return first.depth < second.depth;
				}

				return first.key < second.key;
			});

		std::vector<PlacedPlate> placed;
		placed.reserve(boxes.size());

		for (const size_t index : order)
		{
			const PlazaNameplateBox& box = boxes[index];
			const float halfWidth = box.width * 0.5f;
			const float height = std::max(box.height, 0.0f);

			// The anchor itself may poke out of the screen: start inside it.
			float anchorBottom = box.bottom;
			anchorBottom = std::max(anchorBottom, screenTop + height);
			anchorBottom = std::min(anchorBottom, screenBottom);

			// Up, above every placed plate in the way. Each pass moves strictly above one more plate,
			// so it ends after at most one pass per placed plate.
			float bottom = anchorBottom;
			for (size_t pass = 0; pass <= placed.size(); ++pass)
			{
				bool moved = false;
				for (const PlacedPlate& other : placed)
				{
					if (Overlaps(other, box.x, halfWidth, bottom - height, bottom, gap, box.hysteresis))
					{
						if (other.top - gap < bottom)
						{
							bottom = other.top - gap;
							moved = true;
						}
					}
				}

				if (!moved)
				{
					break;
				}
			}

			if (bottom - height < screenTop)
			{
				// No room above: below the plates in the way instead, if that stays on screen.
				float below = anchorBottom;
				for (size_t pass = 0; pass <= placed.size(); ++pass)
				{
					bool moved = false;
					for (const PlacedPlate& other : placed)
					{
						if (Overlaps(other, box.x, halfWidth, below - height, below, gap, box.hysteresis))
						{
							if (other.bottom + gap + height > below)
							{
								below = other.bottom + gap + height;
								moved = true;
							}
						}
					}

					if (!moved)
					{
						break;
					}
				}

				bottom = below <= screenBottom ? below : screenTop + height;
			}

			placed.push_back(PlacedPlate{ box.x, halfWidth, bottom - height, bottom });
			offsets[index] = bottom - box.bottom;
		}

		return offsets;
	}

	const std::vector<float>& PlazaNameplateDeclutter::Update(const std::vector<PlazaNameplateBox>& boxes, const float deltaSeconds, const float gap, const float screenTop, const float screenBottom)
	{
		std::vector<PlazaNameplateBox> withHysteresis = boxes;
		for (PlazaNameplateBox& box : withHysteresis)
		{
			const auto it = m_moved.find(box.key);
			box.hysteresis = (it != m_moved.end() && it->second) ? Hysteresis : 0.0f;
		}

		const std::vector<float> targets = ResolvePlazaNameplateOffsets(withHysteresis, gap, screenTop, screenBottom);

		m_moved.clear();
		for (size_t i = 0; i < boxes.size(); ++i)
		{
			m_moved[boxes[i].key] = targets[i] != 0.0f;
		}
		const float follow = 1.0f - std::exp(-EaseRate * std::max(deltaSeconds, 0.0f));

		std::map<int32, float> offsets;
		m_result.resize(boxes.size());

		for (size_t i = 0; i < boxes.size(); ++i)
		{
			const int32 key = boxes[i].key;
			const float target = targets[i];

			float offset = target;
			if (const auto it = m_offsets.find(key); it != m_offsets.end())
			{
				offset = it->second + (target - it->second) * follow;
				if (std::abs(target - offset) < 0.01f)
				{
					offset = target;
				}
			}

			offsets[key] = offset;
			m_result[i] = offset;
		}

		m_offsets = std::move(offsets);
		return m_result;
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_updrafts.h"

#include "shared/proto_data/area_triggers.pb.h"

namespace mmo
{
	std::vector<UpdraftVolume> BuildBotUpdraftVolumes(const proto::AreaTriggers& triggers, const uint32 mapId)
	{
		std::vector<UpdraftVolume> volumes;
		for (const auto& trigger : triggers.entry())
		{
			if (trigger.map() != mapId || !trigger.has_updraft_strength() || trigger.updraft_strength() <= 0.0f)
			{
				continue;
			}

			UpdraftVolume volume;
			volume.center = Vector3(trigger.x(), trigger.y(), trigger.z());
			volume.strength = trigger.updraft_strength();

			if (trigger.has_radius())
			{
				if (trigger.radius() <= 0.0f)
				{
					continue;
				}

				volume.radius = trigger.radius();
			}
			else if (trigger.has_box_x() && trigger.has_box_y() && trigger.has_box_z())
			{
				volume.halfExtents = Vector3(trigger.box_x() * 0.5f, trigger.box_y() * 0.5f, trigger.box_z() * 0.5f);
				volume.orientation = trigger.has_box_o() ? trigger.box_o() : 0.0f;
			}
			else
			{
				continue;
			}

			volumes.push_back(volume);
		}

		return volumes;
	}
}

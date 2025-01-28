#pragma once

#include "game_object_c.h"

namespace mmo
{
	class GameWorldObjectC_Base : public GameObjectC
	{
	public:
		explicit GameWorldObjectC_Base(Scene& scene, const proto_client::Project& project);
		virtual ~GameWorldObjectC_Base() override = default;

	public:
		virtual GameWorldObjectType GetType() const = 0;
	};

	class GameWorldObjectC_Chest final : public GameWorldObjectC_Base
	{
	public:
		explicit GameWorldObjectC_Chest(Scene& scene, const proto_client::Project& project);
		~GameWorldObjectC_Chest() override = default;

	public:
		GameWorldObjectType GetType() const override { return game_world_object_type::Chest; }
	};
}
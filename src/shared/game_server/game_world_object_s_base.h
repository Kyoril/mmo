#pragma once

#include "game_object_s.h"

namespace mmo
{
	class GameWorldObjectS_Base : public GameObjectS
	{
	public:
		explicit GameWorldObjectS_Base(const proto::Project& project);
		virtual ~GameWorldObjectS_Base() override = default;

	public:
		virtual GameWorldObjectType GetType() const = 0;

		virtual bool IsUsable() const { return false; }
	};

	class GameWorldObjectS_Chest final : public GameWorldObjectS_Base
	{
	public:
		explicit GameWorldObjectS_Chest(const proto::Project& project);
		~GameWorldObjectS_Chest() override = default;

	public:
		void Initialize() override;

	protected:
		void PrepareFieldMap() override;

	public:
		GameWorldObjectType GetType() const override { return game_world_object_type::Chest; }
	};
}
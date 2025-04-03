#pragma once

#include "game_object_s.h"

namespace mmo
{
	class GameWorldObjectS_Base : public GameObjectS
	{
	public:
		explicit GameWorldObjectS_Base(const proto::Project& project, const proto::ObjectEntry& entry);
		virtual ~GameWorldObjectS_Base() override = default;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Object; }

	public:
		virtual void Initialize() override;

	public:
		virtual GameWorldObjectType GetType() const = 0;

		virtual bool IsUsable() const { return false; }

		const String& GetName() const override;

		bool HasMovementInfo() const override { return true; }

	protected:
		void PrepareFieldMap() override;

	protected:
		const proto::ObjectEntry& m_entry;
	};

	class GameWorldObjectS_Chest final : public GameWorldObjectS_Base
	{
	public:
		explicit GameWorldObjectS_Chest(const proto::Project& project, const proto::ObjectEntry& entry);
		~GameWorldObjectS_Chest() override = default;

	public:
		void Initialize() override;

	public:
		GameWorldObjectType GetType() const override { return game_world_object_type::Chest; }
	};
}
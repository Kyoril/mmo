#pragma once

#include "game_object_s.h"
#include "loot_instance.h"

namespace mmo
{
	class GameWorldObjectS : public GameObjectS
	{
	public:
		explicit GameWorldObjectS(const proto::Project& project, const proto::ObjectEntry& entry);
		virtual ~GameWorldObjectS() override = default;

		ObjectTypeId GetTypeId() const override { return ObjectTypeId::Object; }

	public:
		virtual void Initialize() override;

	public:
		GameWorldObjectType GetType() const { return static_cast<GameWorldObjectType>(Get<uint32>(object_fields::ObjectTypeId)); }

		bool IsUsable() const;

		void Use(GamePlayerS& player);

		const String& GetName() const override;

		bool HasMovementInfo() const override { return true; }

	protected:
		void PrepareFieldMap() override;

		void OnLootClosed(uint64 lootGuid);

		void OnLootCleared();

	protected:
		const proto::ObjectEntry& m_entry;
		scoped_connection_container m_lootSignals;
	};
}

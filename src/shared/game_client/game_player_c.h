#pragma once

#include "game_object_c.h"
#include "game_unit_c.h"
#include "game/movement_info.h"

namespace mmo
{
	namespace proto_client
	{
		class ClassEntry;
	}

	struct GuildInfo;
}

namespace mmo
{
	class GamePlayerC : public GameUnitC
	{
	public:
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, ICollisionProvider& collisionProvider, const proto_client::Project& project, uint32 map)
			: GameUnitC(scene, netDriver, collisionProvider, project, map)
		{
		}

		virtual ~GamePlayerC() override;

		virtual ObjectTypeId GetTypeId() const override
		{
			return ObjectTypeId::Player;
		}

		virtual void Deserialize(io::Reader& reader, bool complete) override;

		virtual void Update(float deltaTime) override;

		virtual void InitializeFieldMap() override;

		virtual const String& GetName() const override;

		void SetName(const String& name);

		uint8 GetAttributeCost(uint32 attribute) const;

		void NotifyItemData(const ItemInfo& data);

		void NotifyGuildInfo(const GuildInfo* guild);

		uint32 GetAvailableAttributePoints() const override
		{
			return Get<uint32>(object_fields::AvailableAttributePoints);
		}

		uint32 GetTalentPoints() const override
		{
			return Get<uint32>(object_fields::TalentPoints);
		}

		const proto_client::ClassEntry* GetClass() const;

	protected:
		virtual void SetupSceneObjects() override;

		void OnEquipmentChanged(uint64);

		void RefreshDisplay();

		void OnGuildChanged(uint64);

		virtual void RefreshUnitName() override;

	protected:
		String m_name;

		TagPoint* m_shieldAttachment{ nullptr };
		Entity* m_shieldEntity{ nullptr };

		TagPoint* m_weaponAttachment{ nullptr };
		Entity* m_weaponEntity{ nullptr };

		scoped_connection m_equipmentChangedHandler;
		scoped_connection m_guildChangedHandler;

		const GuildInfo* m_guild{ nullptr };
	};
}

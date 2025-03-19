#pragma once

#include "game_object_c.h"
#include "game_unit_c.h"
#include "game/movement_info.h"

namespace mmo
{
	class GamePlayerC : public GameUnitC
	{
	public:
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, ICollisionProvider& collisionProvider, const proto_client::Project& project)
			: GameUnitC(scene, netDriver, collisionProvider, project)
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

		void SetName(const String& name)
		{
			m_name = name;
		}

		uint8 GetAttributeCost(uint32 attribute) const;

	protected:
		virtual void SetupSceneObjects() override;

		void OnEquipmentChanged(uint64);

	protected:
		String m_name;

		TagPoint* m_shieldAttachment{ nullptr };
		Entity* m_shieldEntity{ nullptr };

		TagPoint* m_weaponAttachment{ nullptr };
		Entity* m_weaponEntity{ nullptr };

		scoped_connection m_equipmentChangedHandler;
	};
}

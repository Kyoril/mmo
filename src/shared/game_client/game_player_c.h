#pragma once

#include "game_object_c.h"
#include "game_unit_c.h"
#include "game/movement_info.h"

namespace mmo
{
	class GamePlayerC : public GameUnitC
	{
	public:
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, ICollisionProvider& collisionProvider)
			: GameUnitC(scene, netDriver, collisionProvider)
		{
		}

		virtual ~GamePlayerC() override = default;

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

	protected:
		virtual void SetupSceneObjects() override;

	protected:
		String m_name;
	};
}

#pragma once

#include "game_client/game_unit_c.h"
#include "test_scene.h"
#include "test_net_client.h"

namespace mmo
{
	namespace proto_client { class Project; }

	/// @brief Minimal GameUnitC subclass for use in headless movement tests.
	class TestGameUnit final : public GameUnitC
	{
	public:
		explicit TestGameUnit(TestScene& scene, TestNetClient& net,
			const proto_client::Project& project, uint32 map)
			: GameUnitC(scene, net, project, map)
		{
		}

		/// @brief Override the collider for testing (e.g. place capsule at specific position).
		void SetColliderForTest(const Capsule& c)
		{
			const_cast<Capsule&>(GetCollider()) = c;
		}
	};
}

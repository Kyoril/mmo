#pragma once

#include "game_item_c.h"

namespace mmo
{
	class NetClient;

	class GameBagC : public GameItemC
	{
	public:
		explicit GameBagC(Scene& scene, NetClient& netDriver, const proto_client::Project& project);
		virtual ~GameBagC() override = default;

		void Deserialize(io::Reader& reader, bool complete) override;

		virtual ObjectTypeId GetTypeId() const override { return ObjectTypeId::Container; }

	public:
		virtual void InitializeFieldMap() override;
	};
}

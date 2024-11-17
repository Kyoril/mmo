#pragma once

#include "game_object_c.h"

namespace mmo
{
	class NetClient;

	class GameItemC : public GameObjectC
	{
	public:
		explicit GameItemC(Scene& scene, NetClient& netDriver, const proto_client::Project& project);
		virtual ~GameItemC() override = default;

		void Deserialize(io::Reader& reader, bool complete) override;

		virtual ObjectTypeId GetTypeId() const override { return ObjectTypeId::Item; }

		void NotifyItemData(const ItemInfo& info) { m_info = &info; }

		const ItemInfo* GetEntry() const { return m_info; }

	public:
		virtual void InitializeFieldMap() override;

	private:
		NetClient& m_netDriver;
		const ItemInfo* m_info = nullptr;
	};
}

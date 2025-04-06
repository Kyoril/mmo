#pragma once

#include "game_object_c.h"

namespace mmo
{
	struct ObjectInfo;
	class NetClient;

	class GameWorldObjectC_Base : public GameObjectC
	{
	public:
		explicit GameWorldObjectC_Base(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		virtual ~GameWorldObjectC_Base() override = default;

		void InitializeFieldMap() override;

		void Deserialize(io::Reader& reader, bool complete) override;

		virtual void NotifyObjectData(const	ObjectInfo& data);

		const ObjectInfo* GetEntry() const noexcept { return m_entry; }

	protected:
		virtual void SetupSceneObjects();

		virtual void OnDisplayIdChanged();

		virtual void OnEntryChanged();

	public:
		virtual GameWorldObjectType GetType() const = 0;

	protected:
		NetClient& m_netDriver;
		const ObjectInfo* m_entry = nullptr;
	};

	class GameWorldObjectC_Chest final : public GameWorldObjectC_Base
	{
	public:
		explicit GameWorldObjectC_Chest(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		~GameWorldObjectC_Chest() override = default;

	public:
		GameWorldObjectType GetType() const override { return game_world_object_type::Chest; }
	};
}
#pragma once

#include "game_object_c.h"
#include "game_world_object_c_type_base.h"

namespace mmo
{
	struct ObjectInfo;
	class NetClient;

	class GameWorldObjectC : public GameObjectC
	{
	public:
		explicit GameWorldObjectC(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		virtual ~GameWorldObjectC() override = default;

		void InitializeFieldMap() override;

		void Deserialize(io::Reader& reader, bool complete) override;

		virtual void NotifyObjectData(const	ObjectInfo& data);

		const ObjectInfo* GetEntry() const noexcept { return m_entry; }

	protected:
		virtual void SetupSceneObjects();

		virtual void OnDisplayIdChanged();

		virtual void OnEntryChanged();

	public:
		virtual GameWorldObjectType GetType() const { return static_cast<GameWorldObjectType>(Get<uint32>(object_fields::ObjectTypeId)); }

	protected:
		NetClient& m_netDriver;
		const ObjectInfo* m_entry = nullptr;
		std::unique_ptr<GameWorldObjectC_Type_Base> m_typeData;
	};
}

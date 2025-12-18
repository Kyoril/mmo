#pragma once

#include "game_object_c.h"
#include "game_world_object_c_type_base.h"

namespace mmo
{
	struct ObjectInfo;
	class NetClient;
	class GamePlayerC;

	/// @brief Object flags for world objects (stored in ObjectFlags field).
	namespace world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can only be used when a specific quest is active.
			RequiresQuest = 0x01,
			/// Object is temporarily disabled (e.g., by server script).
			Disabled = 0x02,
		};
	}

	class GameWorldObjectC : public GameObjectC
	{
	public:
		explicit GameWorldObjectC(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		virtual ~GameWorldObjectC() override = default;

		void InitializeFieldMap() override;

		void Deserialize(io::Reader& reader, bool complete) override;

		virtual void NotifyObjectData(const	ObjectInfo& data);

		const ObjectInfo* GetEntry() const { return m_entry; }

		/// @brief Checks if this object can be used by the given player.
		/// @param player The player attempting to use the object.
		/// @return true if the object is usable, false otherwise.
		bool IsUsable(const GamePlayerC& player) const override;

	protected:
		virtual void SetupSceneObjects();

		virtual void OnDisplayIdChanged();

		virtual void OnEntryChanged();

		/// @brief Gets the quest ID required to use this object, if any.
		/// @return Quest ID or 0 if no quest is required.
		virtual uint32 GetRequiredQuestId() const
		{
			return 0;
		}

	public:
		virtual GameWorldObjectType GetType() const { return static_cast<GameWorldObjectType>(Get<uint32>(object_fields::ObjectTypeId)); }

	protected:
		NetClient& m_netDriver;
		const ObjectInfo* m_entry = nullptr;
		std::unique_ptr<GameWorldObjectC_Type_Base> m_typeData;
	};
}

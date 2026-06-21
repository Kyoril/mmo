#pragma once

#include "game_object_c.h"
#include "game_world_object_c_type_base.h"

namespace mmo
{
	struct ObjectInfo;
	class NetClient;
	class GamePlayerC;
	class ParticleSystem;
	class SceneNode;

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

	/// @brief Per-player dynamic flags computed by server.
	namespace dynamic_world_object_flags
	{
		enum Type : uint32
		{
			/// No special flags.
			None = 0x00,
			/// Object can be interacted with by this player.
			Interactable = 0x01,
		};
	}

	class GameWorldObjectC : public GameObjectC
	{
	public:
		explicit GameWorldObjectC(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		virtual ~GameWorldObjectC() override;

		void InitializeFieldMap() override;

		void Deserialize(io::Reader& reader, bool complete) override;

		/// @copydoc GameObjectC::GetName
		const String& GetName() const override;

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
		/// @brief Creates or destroys the spark particle emitter based on the interactable flag.
		/// @param active If true, creates and plays the emitter; if false, destroys it.
		void UpdateSparkEmitter(bool active);

	protected:
		NetClient& m_netDriver;
		const ObjectInfo* m_entry = nullptr;
		std::unique_ptr<GameWorldObjectC_Type_Base> m_typeData;

	private:
		ParticleSystem* m_sparkEmitter = nullptr;
		SceneNode* m_sparkEmitterNode = nullptr;
	};
}

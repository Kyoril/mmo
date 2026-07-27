#pragma once

#include "game_object_c.h"
#include "game_world_object_c_type_base.h"
#include "game/world_object_flags.h"

namespace mmo
{
	struct ObjectInfo;
	class NetClient;
	class GamePlayerC;
	class ParticleSystem;
	class SceneNode;
	class AnimationState;

	class GameWorldObjectC : public GameObjectC
	{
	public:
		explicit GameWorldObjectC(Scene& scene, const proto_client::Project& project, NetClient& netDriver, uint32 map);
		virtual ~GameWorldObjectC() override;

		void InitializeFieldMap() override;

		void Deserialize(io::Reader& reader, bool complete) override;

		/// @brief Advances the door open/close animation, if one is playing.
		void Update(float deltaTime) override;

		/// @copydoc GameObjectC::GetName
		const String& GetName() const override;

		virtual void NotifyObjectData(const	ObjectInfo& data);

		const ObjectInfo* GetEntry() const { return m_entry; }

		/// @brief Checks if this object can be used by the given player.
		/// @param player The player attempting to use the object.
		/// @return true if the object is usable, false otherwise.
		bool IsUsable(const GamePlayerC& player) const override;

		/// @brief Checks if this object is currently flagged as interactable for the local player.
		/// @return true if the Interactable dynamic flag is set, false otherwise.
		bool IsInteractable() const
		{
			return (Get<uint32>(object_fields::DynamicObjectFlags) & dynamic_world_object_flags::Interactable) != 0;
		}

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

		/// @brief Applies the door's visual and collision state from the State field. No-op for
		/// non-door objects. Collision is disabled the instant opening starts and re-enabled the
		/// instant closing starts, matching the server's edge-triggered line of sight blocking.
		/// @param animate true to play the Open/Close clip, false to snap to the settled pose
		/// (initial spawn, mesh swap).
		void ApplyDoorState(bool animate);

	protected:
		NetClient& m_netDriver;
		const ObjectInfo* m_entry = nullptr;
		std::unique_ptr<GameWorldObjectC_Type_Base> m_typeData;

	private:
		ParticleSystem* m_sparkEmitter = nullptr;
		SceneNode* m_sparkEmitterNode = nullptr;

		/// @brief Currently advancing door animation state (owned by the entity), if any.
		AnimationState* m_doorAnimState = nullptr;
	};
}

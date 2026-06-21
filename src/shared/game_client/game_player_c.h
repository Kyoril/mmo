#pragma once

#include "game_object_c.h"
#include "game_unit_c.h"
#include "game/movement_info.h"
#include "scene_graph/manual_render_object.h"

namespace mmo
{
	namespace proto_client
	{
		class ClassEntry;
		class ItemDisplayVariant;
		class ItemDisplayBoneAttachment;
	}

	struct GuildInfo;
	class IAudio;
}

namespace mmo
{
	class GamePlayerC : public GameUnitC
	{
	public:
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, const proto_client::Project& project, uint32 map, IAudio* audio)
			: GameUnitC(scene, netDriver, project, map)
			, m_audio(audio)
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

		void SetName(const String& name);

		uint8 GetAttributeCost(uint32 attribute) const override;

		void NotifyItemData(const ItemInfo& data);

		void NotifyGuildInfo(const GuildInfo* guild);

		/// Gets the name of the player's guild.
		/// @return The guild name, or an empty string if the player has no known guild.
		[[nodiscard]] const String& GetGuildName() const;

		uint32 GetAvailableAttributePoints() const override
		{
			return Get<uint32>(object_fields::AvailableAttributePoints);
		}

		uint32 GetTalentPoints() const override
		{
			return Get<uint32>(object_fields::TalentPoints);
		}

		const proto_client::ClassEntry* GetClass() const;

	protected:
		virtual void SetupSceneObjects() override;

		void OnDisplayIdChanged() override;

		void OnEquipmentChanged(uint64);

		void RefreshDisplay();

		void OnGuildChanged(uint64);

		virtual void RefreshUnitName() override;

		void ClearAllAttachments();

		/// Reacts to unit flag changes and toggles weapon attachments between their drawn and
		/// sheathed bones when the combat state flips.
		void OnUnitFlagsChanged(uint64);

		/// Re-attaches all weapon attachments to their drawn or sheathed bone based on the current
		/// weapon-drawn state.
		void RefreshWeaponAttachmentBones();

		/// Selects the bone attachment to use for a variant given the current draw state, falling
		/// back to the default attachment when a state-specific one is not defined.
		static const proto_client::ItemDisplayBoneAttachment* SelectBoneAttachment(const proto_client::ItemDisplayVariant& variant, bool drawn);

		/// Applies a bone attachment's offset, rotation and scale to a tag point.
		static void ApplyBoneTransform(TagPoint* tagPoint, const proto_client::ItemDisplayBoneAttachment& bone);

		/// Register footstep notification handlers for all animations
		void RegisterFootstepHandlers();

		/// Handle footstep notification trigger
		void OnFootstep(const class AnimationNotify& notify);

	private:


	protected:
		String m_name;

		scoped_connection m_equipmentChangedHandler;
		scoped_connection m_guildChangedHandler;

		const GuildInfo* m_guild{ nullptr };
		IAudio* m_audio{ nullptr };

		struct ItemAttachment
		{
			Entity* entity{ nullptr };
			TagPoint* attachment{ nullptr };
			/// The display variant this attachment was created from. Used to re-resolve the
			/// correct bone (drawn vs. sheathed) when the combat state changes.
			const proto_client::ItemDisplayVariant* variant{ nullptr };
		};

		std::unordered_map<uint32, ItemAttachment> m_itemAttachments;

		scoped_connection m_unitFlagsChangedHandler;

		/// Tracks the last known weapon-drawn state so we only re-attach weapons when it flips.
		bool m_weaponsDrawn{ false };

		scoped_connection_container m_animNotifyConnections;
	};
}

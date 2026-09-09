#pragma once

#include "game_object_c.h"
#include "game_unit_c.h"
#include "item_display_applier.h"
#include "game/item.h"
#include "game/movement_info.h"
#include "scene_graph/manual_render_object.h"

#include <array>

namespace mmo
{
	namespace proto_client
	{
		class ClassEntry;
	}

	struct GuildInfo;
	class IAudio;
	class SoundEntryPlayer;
}

namespace mmo
{
	class GamePlayerC : public GameUnitC
	{
	public:
		explicit GamePlayerC(Scene& scene, NetClient& netDriver, const proto_client::Project& project, uint32 map, IAudio* audio, SoundEntryPlayer* soundEntryPlayer)
			: GameUnitC(scene, netDriver, project, map)
			, m_audio(audio)
			, m_soundEntryPlayer(soundEntryPlayer)
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

		/// One entry of the character's known-class list, replicated from the server (see the
		/// KnownClasses packet). The active class is the one in object_fields::Class.
		struct KnownClassInfo
		{
			uint32 classId = 0;
			bool isKnown = false;
			uint8 classLevel = 1;
			uint8 maxClassLevel = 1;
			uint32 classXp = 0;
			uint32 xpToNextLevel = 0;
			/// The class-change spell that switches the active class to this one (0 if none). Cast it
			/// to become this class.
			uint32 changeSpellId = 0;
		};

		/// Replaces the locally-cached class overview. It contains every configured class so the UI can
		/// show locked entries, plus progression and switch data for classes the character knows.
		void SetKnownClasses(std::vector<KnownClassInfo> knownClasses) { m_knownClasses = std::move(knownClasses); }

		/// Returns the cached class overview. Check KnownClassInfo::isKnown before offering interaction.
		[[nodiscard]] const std::vector<KnownClassInfo>& GetKnownClasses() const { return m_knownClasses; }

		/// Updates the cached progression values of a single known class in place (class XP gain
		/// without a full KnownClasses refresh). Does nothing if the class is not in the cache.
		void UpdateKnownClassProgress(const uint32 classId, const uint8 classLevel, const uint32 classXp, const uint32 xpToNextLevel)
		{
			for (auto& info : m_knownClasses)
			{
				if (info.classId == classId)
				{
					info.classLevel = classLevel;
					info.classXp = classXp;
					info.xpToNextLevel = xpToNextLevel;
					break;
				}
			}
		}

		/// Returns the class level of the currently active class (defaults to 1).
		[[nodiscard]] uint32 GetActiveClassLevel() const
		{
			for (const auto& info : m_knownClasses)
			{
				if (info.classId == m_activeKnownClassId)
				{
					return info.classLevel == 0 ? 1 : info.classLevel;
				}
			}

			return 1;
		}

		/// Sets the active class id reported alongside the known-class list. Cached separately from the
		/// replicated Class field so the class list refreshes consistently from the KnownClasses packet
		/// regardless of field-update timing.
		void SetActiveKnownClassId(uint32 classId) { m_activeKnownClassId = classId; }

		/// Returns the active class id reported by the last KnownClasses packet (0 if unknown).
		[[nodiscard]] uint32 GetActiveKnownClassId() const { return m_activeKnownClassId; }

		/// Resolves the class definition for a known-class list entry, or nullptr if the index is out
		/// of range or the class id is unknown to the client project.
		[[nodiscard]] const proto_client::ClassEntry* GetKnownClassEntry(size_t index) const;

		/// Replaces the set of unlocked (non-default) emote ids (from the InitialEmotes packet).
		void SetKnownEmotes(const std::vector<uint32>& emoteIds) { m_knownEmoteIds.clear(); m_knownEmoteIds.insert(emoteIds.begin(), emoteIds.end()); }

		/// Adds a newly unlocked emote id (from the EmoteLearned packet).
		void AddKnownEmote(const uint32 emoteId) { m_knownEmoteIds.insert(emoteId); }

		/// Returns true if this player can use the given emote (default-known or unlocked).
		[[nodiscard]] bool KnowsEmote(uint32 emoteId) const;

		/// Returns the unlocked (non-default) emote ids.
		[[nodiscard]] const std::set<uint32>& GetKnownEmoteIds() const { return m_knownEmoteIds; }


	public:
		/// Fired whenever the equipment visuals of this player changed: the set of equipped items
		/// was replaced, or the display data of one of them arrived and was applied. UI model
		/// frames listen to this to mirror the in-world appearance of the character.
		signal<void()> equipmentVisualsChanged;

		/// Item display ids of the currently equipped, visible items, indexed by equipment slot
		/// (see player_equipment_slots). A zero entry means the slot is empty or the item data of
		/// that slot has not arrived from the server yet.
		[[nodiscard]] const std::array<uint32, player_equipment_slots::Count_>& GetEquipmentDisplayIds() const { return m_equipmentDisplayIds; }

	public:
		/// @copydoc GameUnitC::GetWeaponItemSubclass
		[[nodiscard]] uint32 GetWeaponItemSubclass(bool offhand) const override
		{
			return offhand ? m_offHandSubclass : m_mainHandSubclass;
		}

		/// @copydoc GameUnitC::GetChestItemSubclass
		[[nodiscard]] uint32 GetChestItemSubclass() const override
		{
			return m_chestSubclass;
		}

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

		/// Register footstep notification handlers for all animations
		void RegisterFootstepHandlers();

		/// Handle footstep notification trigger
		void OnFootstep(const class AnimationNotify& notify);

		/// @brief Legacy fallback footstep playback (hardcoded ground WAVs) used while
		///        surface types / footstep sound entries are not authored yet.
		void PlayLegacyFootstepSound();

	private:
		/// Item subclass of the equipped main hand weapon. 0 = unarmed.
		uint32 m_mainHandSubclass = 0;

		/// Item subclass of the equipped off hand weapon or shield. 0 = empty.
		uint32 m_offHandSubclass = 0;

		/// Item subclass of the equipped chest armor. 0 = none.
		uint32 m_chestSubclass = 0;

	protected:
		String m_name;

		scoped_connection m_equipmentChangedHandler;
		scoped_connection m_guildChangedHandler;

		const GuildInfo* m_guild{ nullptr };
		IAudio* m_audio{ nullptr };
		SoundEntryPlayer* m_soundEntryPlayer{ nullptr };

		/// Cached class overview, including locked classes and progression data for unlocked classes,
		/// replicated from the server via the KnownClasses packet.
		std::vector<KnownClassInfo> m_knownClasses;

		/// Active class id reported by the last KnownClasses packet (0 if none received yet).
		uint32 m_activeKnownClassId{ 0 };

		/// Unlocked (non-default) emote ids, replicated via InitialEmotes / EmoteLearned.
		std::set<uint32> m_knownEmoteIds;

		ItemDisplayAttachmentMap m_itemAttachments;

		/// Item display ids of the currently equipped, visible items, indexed by equipment slot.
		std::array<uint32, player_equipment_slots::Count_> m_equipmentDisplayIds{};

		scoped_connection m_unitFlagsChangedHandler;

		/// Tracks the last known weapon-drawn state so we only re-attach weapons when it flips.
		bool m_weaponsDrawn{ false };

		scoped_connection_container m_animNotifyConnections;
	};
}

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"

#include <unordered_map>
#include <vector>

struct lua_State;

namespace mmo
{
	class RealmConnector;

	struct TalentTabInfo
	{
		uint32 id = 0;
		String name;
		String icon;
		String background;
		uint32 canvasWidth = 1600;
		uint32 canvasHeight = 1200;
		float initialZoom = 1.0f;
	};

	struct TalentPrerequisiteInfo
	{
		uint32 talentId = 0;
		uint32 requiredRank = 1;
		uint32 currentRank = 0;
		bool met = false;
	};

	struct TalentInfo
	{
		uint32 id = 0;
		uint32 tabId = 0;
		uint32 tier = 0;
		uint32 column = 0;
		uint32 spellId = 0;
		const proto_client::SpellEntry* spell = nullptr;
		const proto_client::SpellEntry* nextRankSpell = nullptr;
		int32 rank = -1;
		uint32 maxRank = 0;
		int32 positionX = 0;
		int32 positionY = 0;
		uint32 requiredPoints = 0;
		float nodeScale = 1.0f;
		bool canLearn = false;
		String icon;
		String name;
		std::vector<TalentPrerequisiteInfo> prerequisites;
		TalentInfo(uint32 id, uint32 tabId, uint32 tier, uint32 column, uint32 spellId, 
				   const proto_client::SpellEntry* spell, const proto_client::SpellEntry* nextRankSpell,
				   int32 rank, uint32 maxRank, int32 positionX, int32 positionY, uint32 requiredPoints,
				   float nodeScale, const String& icon, const String& name)
			: id(id), tabId(tabId), tier(tier), column(column), spellId(spellId)
			, spell(spell), nextRankSpell(nextRankSpell), rank(rank), maxRank(maxRank)
			, positionX(positionX), positionY(positionY), requiredPoints(requiredPoints), nodeScale(nodeScale)
			, icon(icon), name(name)
		{
		}
	};
	class TalentClient final : public NonCopyable
	{
	public:
		TalentClient(const proto_client::TalentTabManager& tabManager, const proto_client::TalentManager& talentManager,
			const proto_client::SpellManager& spellManager, RealmConnector& realmConnector);

	public:
		void Initialize();

		void Shutdown();

		void RegisterScriptFunctions(lua_State* luaState);

		void NotifyCharacterClassChanged();

		/// Notifies the talent client that a spell has been learned.
		/// This will update talent ranks for any talents that use this spell.
		void OnSpellLearned(uint32 spellId);

		/// Notifies the talent client that a spell has been unlearned.
		/// This will update talent ranks for any talents that use this spell.
		void OnSpellUnlearned(uint32 spellId);

	private:
		void RebuildTalentTrees();

		/// Updates talent ranks based on currently known spells
		void UpdateTalentRanks();
		/// Gets the total number of talent points spent in a specific tab
		uint32 GetTalentPointsSpentInTab(uint32 tabId) const;

		/// Gets the total number of talent points spent in a specific tab by index
		uint32 GetTalentPointsSpentInTabByIndex(int32 tabIndex) const;

		int32 GetNumTalentTabs() const;

		const char* GetTalentTabName(int32 index);
		const TalentTabInfo* GetTalentTabInfo(int32 index);
		int32 GetNumTalents(int32 tabIndex);

		const TalentInfo* GetTalentInfo(int32 tabIndex, int32 talentIndex);

		bool LearnTalent(uint32 tabIndex, int32 talentIndex);
	private:
		const proto_client::TalentTabManager& m_tabManager;
		const proto_client::TalentManager& m_talentManager;
		const proto_client::SpellManager& m_spellManager;
		std::unordered_map<uint32, std::vector<TalentInfo>> m_talentsByTreeId;
		std::unordered_map<uint32, uint32> m_talentPointsSpentPerTab;
		std::vector<TalentTabInfo> m_tabs;
		RealmConnector& m_realmConnector;
	};
}

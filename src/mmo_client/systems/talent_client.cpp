#include "talent_client.h"

#include "luabind_lambda.h"
#include "game_client/object_mgr.h"
#include "net/realm_connector.h"
#include "frame_ui/frame_mgr.h"

namespace mmo
{
	TalentClient::TalentClient(const proto_client::TalentTabManager& tabManager, const proto_client::TalentManager& talentManager, const proto_client::SpellManager& spellManager, RealmConnector& realmConnector)
		: m_tabManager(tabManager)
		, m_talentManager(talentManager)
		, m_spellManager(spellManager)
		, m_realmConnector(realmConnector)
	{
	}

	void TalentClient::Initialize()
	{
		// Initialize the talent trees

	}

	void TalentClient::Shutdown()
	{
	}

	void TalentClient::RegisterScriptFunctions(lua_State* luaState)
	{
		LUABIND_MODULE(luaState,
			luabind::scope(
				luabind::class_<TalentTabInfo>("TalentTabInfo")
				.def_readonly("id", &TalentTabInfo::id)
				.def_readonly("name", &TalentTabInfo::name)
				.def_readonly("icon", &TalentTabInfo::icon)
				.def_readonly("background", &TalentTabInfo::background)
				.def_readonly("canvasWidth", &TalentTabInfo::canvasWidth)
				.def_readonly("canvasHeight", &TalentTabInfo::canvasHeight)
				.def_readonly("initialZoom", &TalentTabInfo::initialZoom)
			),
			luabind::scope(
				luabind::class_<TalentPrerequisiteInfo>("TalentPrerequisiteInfo")
				.def_readonly("talentId", &TalentPrerequisiteInfo::talentId)
				.def_readonly("requiredRank", &TalentPrerequisiteInfo::requiredRank)
				.def_readonly("currentRank", &TalentPrerequisiteInfo::currentRank)
				.def_readonly("met", &TalentPrerequisiteInfo::met)
			),
			luabind::scope(
				luabind::class_<TalentInfo>("TalentInfo")
				.def_readonly("id", &TalentInfo::id)
				.def_readonly("name", &TalentInfo::name)
				.def_readonly("icon", &TalentInfo::icon)
				.def_readonly("rank", &TalentInfo::rank)
				.def_readonly("maxRank", &TalentInfo::maxRank)
				.def_readonly("spellId", &TalentInfo::spellId)
				.def_readonly("spell", &TalentInfo::spell)
				.def_readonly("nextRankSpell", &TalentInfo::nextRankSpell)
				.def_readonly("tabId", &TalentInfo::tabId)
				.def_readonly("tier", &TalentInfo::tier)
				.def_readonly("column", &TalentInfo::column)
				.def_readonly("positionX", &TalentInfo::positionX)
				.def_readonly("positionY", &TalentInfo::positionY)
				.def_readonly("requiredPoints", &TalentInfo::requiredPoints)
				.def_readonly("nodeScale", &TalentInfo::nodeScale)
				.def_readonly("canLearn", &TalentInfo::canLearn)
			),
				
			luabind::def_lambda("GetNumTalentTabs", [this]() { return GetNumTalentTabs(); }),
			luabind::def_lambda("GetTalentTabName", [this](int32 index) { return GetTalentTabName(index); }),
			luabind::def_lambda("GetTalentTabInfo", [this](int32 index) { return GetTalentTabInfo(index); }),
			luabind::def_lambda("GetNumTalents", [this](int32 tabIndex) { return GetNumTalents(tabIndex); }),
			luabind::def_lambda("GetTalentInfo", [this](int32 tabIndex, int32 talentIndex) { return GetTalentInfo(tabIndex, talentIndex); }),
			luabind::def_lambda("GetNumTalentPrerequisites", [this](int32 tabIndex, int32 talentIndex)
			{
				const TalentInfo* talent = GetTalentInfo(tabIndex, talentIndex);
				return talent ? static_cast<int32>(talent->prerequisites.size()) : 0;
			}),
			luabind::def_lambda("GetTalentPrerequisiteInfo", [this](int32 tabIndex, int32 talentIndex, int32 prerequisiteIndex) -> const TalentPrerequisiteInfo*
			{
				const TalentInfo* talent = GetTalentInfo(tabIndex, talentIndex);
				if (!talent || prerequisiteIndex < 0 || prerequisiteIndex >= static_cast<int32>(talent->prerequisites.size()))
				{
					return nullptr;
				}
				return &talent->prerequisites[prerequisiteIndex];
			}),
			luabind::def_lambda("GetTalentPointsSpentInTab", [this](int32 tabIndex) { return GetTalentPointsSpentInTabByIndex(tabIndex); }),
			luabind::def_lambda("LearnTalent", [this](int32 tabIndex, int32 talentIndex) { return LearnTalent(tabIndex, talentIndex); })
		);
	}
	
	void TalentClient::NotifyCharacterClassChanged()
	{
		RebuildTalentTrees();
		FrameManager::Get().TriggerLuaEvent("PLAYER_TALENT_UPDATE");
	}

	void TalentClient::OnSpellLearned(uint32 spellId)
	{
		UpdateTalentRanks();
		FrameManager::Get().TriggerLuaEvent("PLAYER_TALENT_UPDATE");
	}

	void TalentClient::OnSpellUnlearned(uint32 spellId)
	{
		UpdateTalentRanks();
		FrameManager::Get().TriggerLuaEvent("PLAYER_TALENT_UPDATE");
	}

	void TalentClient::RebuildTalentTrees()
	{
		m_talentsByTreeId.clear();
		m_talentPointsSpentPerTab.clear();
		m_tabs.clear();

		std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return;
		}

		const uint32 playerClass = player->Get<uint32>(object_fields::Class);
		for (const proto_client::TalentTabEntry& tab : m_tabManager.getTemplates().entry())
		{
			if (tab.class_id() != playerClass)
			{
				continue;
			}

			m_tabs.push_back({
				tab.id(),
				tab.name(),
				tab.icon(),
				tab.background(),
				tab.canvas_width(),
				tab.canvas_height(),
				tab.initial_zoom()
			});
			m_talentsByTreeId[tab.id()];
			m_talentPointsSpentPerTab[tab.id()] = 0;
		}

		for (const proto_client::TalentEntry& talent : m_talentManager.getTemplates().entry())
		{
			// Skip empty talent or talent tabs
			if (talent.tab() <= 0 || talent.ranks_size() == 0)
			{
				WLOG("Talent " << talent.id() << " has no tab or ranks, skipping it!");
				continue;
			}

			const proto_client::TalentTabEntry* tab = m_tabManager.getById(talent.tab());
			if (!tab || tab->class_id() != playerClass)
			{
				continue;
			}

			const proto_client::SpellEntry* spell = m_spellManager.getById(talent.ranks(0));
			if (spell == nullptr)
			{
				WLOG("Talent " << talent.id() << " has unknown spell " << talent.ranks(0) << " for rank 0, skipping it!");
				continue;
			}

			auto& entry = m_talentsByTreeId[talent.tab()];
			const int32 positionX = talent.has_position_x()
				? talent.position_x()
				: 160 + static_cast<int32>(talent.column()) * 220;
			const int32 positionY = talent.has_position_y()
				? talent.position_y()
				: 120 + static_cast<int32>(talent.row()) * 200;
			const uint32 requiredPoints = talent.has_required_points()
				? talent.required_points()
				: talent.row() * 5;
			entry.emplace_back(
				talent.id(),
				talent.tab(),
				talent.row(),
				talent.column(),
				spell->id(),
				spell,
				nullptr,
				0, // rank will be updated in UpdateTalentRanks
				static_cast<uint32>(talent.ranks_size()),
				positionX,
				positionY,
				requiredPoints,
				talent.node_scale(),
				spell->icon(),
				spell->name()
			);

			TalentInfo& info = entry.back();
			for (const auto& prerequisite : talent.prerequisites())
			{
				info.prerequisites.push_back({ prerequisite.talent_id(), prerequisite.rank(), 0, false });
			}
		}

		// Update talent ranks based on learned spells
		UpdateTalentRanks();
	}

	void TalentClient::UpdateTalentRanks()
	{
		std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return;
		}

		// Reset talent point counters
		for (auto& pair : m_talentPointsSpentPerTab)
		{
			pair.second = 0;
		}

		// Check each talent in each tab
		for (auto& tabPair : m_talentsByTreeId)
		{
			uint32 tabId = tabPair.first;
			auto& talents = tabPair.second;

			for (auto& talentInfo : talents)
			{
				// Find the talent definition to get all rank spell IDs
				const proto_client::TalentEntry* talentEntry = nullptr;
				for (const auto& talent : m_talentManager.getTemplates().entry())
				{
					if (talent.id() == talentInfo.id)
					{
						talentEntry = &talent;
						break;
					}
				}

				if (!talentEntry)
				{
					continue;
				}
				
				// Check which rank the player has learned
				int32 currentRank = 0;
				for (int32 rank = 0; rank < talentEntry->ranks_size(); ++rank)
				{
					const uint32 spellId = talentEntry->ranks(rank);
					if (player->HasSpell(spellId))
					{
						currentRank = rank + 1; // rank is 1-based
						
						// Update spell reference to the highest rank spell
						talentInfo.spellId = spellId;
						talentInfo.spell = m_spellManager.getById(spellId);
						if (talentInfo.spell)
						{
							talentInfo.icon = talentInfo.spell->icon();
							talentInfo.name = talentInfo.spell->name();
						}
					}
				}

				talentInfo.rank = currentRank;

				// Set next rank spell if not at max rank
				if (currentRank < static_cast<int32>(talentInfo.maxRank) && currentRank < talentEntry->ranks_size())
				{
					const uint32 nextRankSpellId = talentEntry->ranks(currentRank); // currentRank is already 1-based, so this gives us the next rank
					talentInfo.nextRankSpell = m_spellManager.getById(nextRankSpellId);
				}
				else
				{
					talentInfo.nextRankSpell = nullptr;
				}
				
				// Add to talent points spent in this tab
				m_talentPointsSpentPerTab[tabId] += currentRank;
			}
		}

		const uint32 availablePoints = player->GetTalentPoints();
		for (auto& [tabId, talents] : m_talentsByTreeId)
		{
			for (TalentInfo& talent : talents)
			{
				bool prerequisitesMet = true;
				for (TalentPrerequisiteInfo& prerequisite : talent.prerequisites)
				{
					prerequisite.currentRank = 0;
					for (const TalentInfo& candidate : talents)
					{
						if (candidate.id == prerequisite.talentId)
						{
							prerequisite.currentRank = static_cast<uint32>(candidate.rank);
							break;
						}
					}
					prerequisite.met = prerequisite.currentRank >= prerequisite.requiredRank;
					prerequisitesMet = prerequisitesMet && prerequisite.met;
				}

				talent.canLearn = availablePoints > 0
					&& static_cast<uint32>(talent.rank) < talent.maxRank
					&& m_talentPointsSpentPerTab[tabId] >= talent.requiredPoints
					&& prerequisitesMet;
			}
		}
	}

	uint32 TalentClient::GetTalentPointsSpentInTab(uint32 tabId) const
	{
		auto it = m_talentPointsSpentPerTab.find(tabId);
		return (it != m_talentPointsSpentPerTab.end()) ? it->second : 0;
	}

	uint32 TalentClient::GetTalentPointsSpentInTabByIndex(int32 tabIndex) const
	{
		if (tabIndex < 0 || tabIndex >= GetNumTalentTabs())
		{
			return 0;
		}

		return GetTalentPointsSpentInTab(m_tabs[tabIndex].id);
	}

	int32 TalentClient::GetNumTalentTabs() const
	{
		return static_cast<int32>(m_tabs.size());
	}

	const char* TalentClient::GetTalentTabName(const int32 index)
	{
		if (index < 0 || index >= GetNumTalentTabs())
		{
			return nullptr;
		}

		return m_tabs[index].name.c_str();
	}

	const TalentTabInfo* TalentClient::GetTalentTabInfo(const int32 index)
	{
		return index >= 0 && index < static_cast<int32>(m_tabs.size()) ? &m_tabs[index] : nullptr;
	}

	int32 TalentClient::GetNumTalents(const int32 tabIndex)
	{
		if (tabIndex < 0 || tabIndex >= GetNumTalentTabs())
		{
			return 0;
		}

		const auto it = m_talentsByTreeId.find(m_tabs[tabIndex].id);
		return it == m_talentsByTreeId.end() ? 0 : static_cast<int32>(it->second.size());
	}

	const TalentInfo* TalentClient::GetTalentInfo(const int32 tabIndex, const int32 talentIndex)
	{
		if (tabIndex < 0 || tabIndex >= GetNumTalentTabs())
		{
			return nullptr;
		}

		const auto it = m_talentsByTreeId.find(m_tabs[tabIndex].id);
		if (it == m_talentsByTreeId.end())
		{
			return nullptr;
		}

		if (talentIndex < 0 || talentIndex >= static_cast<int32>(it->second.size()))
		{
			return nullptr;
		}

		return &it->second[talentIndex];
	}

	bool TalentClient::LearnTalent(const uint32 tabIndex, const int32 talentIndex)
	{
		const TalentInfo* info = GetTalentInfo(tabIndex, talentIndex);
		if (!info)
		{
			ELOG("Failed to learn talent: Unknown talent");
			return false;
		}

		if (static_cast<uint32>(info->rank) >= info->maxRank)
		{
			WLOG("Unable to learn talent: Max rank already reached");
			return false;
		}
		if (!info->canLearn)
		{
			WLOG("Unable to learn talent: Requirements are not met");
			return false;
		}

		// Note: Since we treat rank as 1-based here rank 0 for us actually means that we don't know the talent yet.
		// So we just pass the current rank to the packet as the server expects it to be 0-based.
		m_realmConnector.LearnTalent(info->id, info->rank);
		return true;
	}
}

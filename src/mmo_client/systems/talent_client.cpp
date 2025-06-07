
#include "talent_client.h"

#include "luabind_lambda.h"
#include "game_client/object_mgr.h"
#include "net/realm_connector.h"

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
		luabind::module(luaState)
		[
			luabind::scope(
				luabind::class_<TalentInfo>("TalentInfo")
				.def_readonly("id", &TalentInfo::id)
				.def_readonly("name", &TalentInfo::name)
				.def_readonly("icon", &TalentInfo::icon)
				.def_readonly("rank", &TalentInfo::rank)
				.def_readonly("maxRank", &TalentInfo::maxRank)
				.def_readonly("spellId", &TalentInfo::spellId)
				.def_readonly("spell", &TalentInfo::spell)
				.def_readonly("tabId", &TalentInfo::tabId)
				.def_readonly("tier", &TalentInfo::tier)
				.def_readonly("column", &TalentInfo::column)),

			luabind::def_lambda("GetNumTalentTabs", [this]() { return GetNumTalentTabs(); }),
			luabind::def_lambda("GetTalentTabName", [this](int32 index) { return GetTalentTabName(index); }),

			luabind::def_lambda("GetNumTalents", [this](int32 tabId) { return GetNumTalents(tabId); }),
			luabind::def_lambda("GetTalentInfo", [this](int32 tabId, int32 index) { return GetTalentInfo(tabId, index); }),
			luabind::def_lambda("LearnTalent", [this](int32 tabId, int32 index) { return LearnTalent(tabId, index); })
		];
	}

	void TalentClient::NotifyCharacterClassChanged()
	{
		RebuildTalentTrees();
	}

	void TalentClient::RebuildTalentTrees()
	{
		m_talentsByTreeId.clear();

		std::shared_ptr<GamePlayerC> player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return;
		}

		const uint32 playerClass = player->Get<uint32>(object_fields::Class);

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
			entry.emplace_back(
				talent.id(),
				talent.tab(),
				talent.row(),
				talent.column(),
				spell->id(),
				spell,
				0,
				static_cast<uint32>(talent.ranks_size() - 1),
				spell->icon(),
				spell->name()
			);
		}
	}

	int32 TalentClient::GetNumTalentTabs()
	{
		return m_talentsByTreeId.size();
	}

	const char* TalentClient::GetTalentTabName(int32 index)
	{
		if (index < 0 || index >= GetNumTalentTabs())
		{
			return nullptr;
		}

		auto it = m_talentsByTreeId.begin();
		if (it == m_talentsByTreeId.end())
		{
			return nullptr;
		}

		std::advance(it, index);
		if (it == m_talentsByTreeId.end())
		{
			return nullptr;
		}

		const proto_client::TalentTabEntry* tab = m_tabManager.getById(it->first);
		if (!tab)
		{
			return nullptr;
		}

		return tab->name().c_str();
	}

	int32 TalentClient::GetNumTalents(const int32 tabId)
	{
		auto it = m_talentsByTreeId.begin();
		if (it == m_talentsByTreeId.end())
		{
			return 0;
		}

		std::advance(it, tabId);

		if (it == m_talentsByTreeId.end())
		{
			return 0;
		}

		return static_cast<int32>(it->second.size());
	}

	const TalentInfo* TalentClient::GetTalentInfo(const int32 tabId, const int32 index)
	{
		auto it = m_talentsByTreeId.begin();
		if (it == m_talentsByTreeId.end())
		{
			return nullptr;
		}

		std::advance(it, tabId);

		if (it == m_talentsByTreeId.end())
		{
			return nullptr;
		}

		if (index < 0 || index >= static_cast<int32>(it->second.size()))
		{
			return nullptr;
		}

		return &it->second[index];
	}

	bool TalentClient::LearnTalent(const uint32 tabId, const int32 index)
	{
		const TalentInfo* info = GetTalentInfo(tabId, index);
		if (!info)
		{
			ELOG("Failed to learn talent: Unknown talent");
			return false;
		}

		if (info->rank >= info->maxRank)
		{
			WLOG("Unable to learn talent: Max rank already reached");
			return false;
		}

		m_realmConnector.LearnTalent(info->id, info->rank + 1);
		return true;
	}
}

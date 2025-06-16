#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"

#include <array>
#include <unordered_map>

struct lua_State;

namespace mmo
{
	class RealmConnector;

	struct TalentInfo
	{
		uint32 id = 0;
		uint32 tabId = 0;
		uint32 tier = 0;
		uint32 column = 0;
		uint32 spellId = 0;
		const proto_client::SpellEntry* spell = nullptr;
		int32 rank = -1;
		uint32 maxRank = 0;
		String icon;
		String name;
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

	private:
		void RebuildTalentTrees();

		int32 GetNumTalentTabs() const;

		const char* GetTalentTabName(int32 index);

		int32 GetNumTalents(int32 tabId);

		const TalentInfo* GetTalentInfo(int32 tabId, int32 index);

		bool LearnTalent(uint32 tabId, int32 index);

	private:
		const proto_client::TalentTabManager& m_tabManager;
		const proto_client::TalentManager& m_talentManager;
		const proto_client::SpellManager& m_spellManager;
		std::unordered_map<uint32, std::vector<TalentInfo>> m_talentsByTreeId;
		RealmConnector& m_realmConnector;
	};
}

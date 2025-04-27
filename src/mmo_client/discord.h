#pragma once

#include "base/signal.h"

#ifdef MMO_WITH_DISCORD_RPC
#	include "discord-rpc/include/discord_rpc.h"
#	include "discord-rpc/include/discord_register.h"
#endif

#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace mmo
{
	class Discord final : public NonCopyable
	{
	public:
		signal<void()> ready;

	public:
		void Initialize();

		void NotifyRealmChanged(const String& realmName);

		void NotifyCharacterData(const String& characterName, uint32 level, const String& className, const String& raceName);

		void NotifyZoneChanged(const String& zoneName);

		void NotifyPartyChanged(bool hasParty, uint32 partySize, uint32 memberCount);

	private:

#ifdef MMO_WITH_DISCORD_RPC
		String m_realmName;
		String m_characterName;
		String m_zoneName;
		bool m_hasParty;
		uint32 m_partySize;
		uint32 m_memberCount;
		DiscordRichPresence m_presence;
#endif
	};
}

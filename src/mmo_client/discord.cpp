
#include "discord.h"

#include "log/default_log_levels.h"
#include "version.h"

namespace mmo
{
	void Discord::Initialize()
	{
#ifdef MMO_WITH_DISCORD_RPC
		DiscordEventHandlers handlers;
		memset(&handlers, 0, sizeof(handlers));
		handlers.ready = [](const DiscordUser* connectedUser)
			{
				ILOG("Connected to Discord as " << connectedUser->username << "#" << connectedUser->discriminator);
			};
		Discord_Initialize(DiscordClientId, &handlers, 1, nullptr);
		ILOG("Discord RPC Initialized");

		std::memset(&m_presence, 0, sizeof(m_presence));
		m_presence.details = "Connecting...";
		m_presence.startTimestamp = time(nullptr);
		m_presence.largeImageKey = "mmorpgserver";
		m_presence.smallImageKey = nullptr;
		Discord_UpdatePresence(&m_presence);
#endif
	}

	void Discord::NotifyRealmChanged(const String& realmName)
	{
#ifdef MMO_WITH_DISCORD_RPC
		m_realmName = realmName;
		m_zoneName.clear();

		m_presence.details = "Character Selection Screen";
		m_presence.largeImageText = m_realmName.c_str();
		m_presence.smallImageKey = nullptr;
		Discord_UpdatePresence(&m_presence);
#endif
	}

	void Discord::NotifyCharacterData(const String& characterName, uint32 level, const String& className, const String& raceName)
	{
#ifdef MMO_WITH_DISCORD_RPC
		m_characterName = characterName + " (Level " + std::to_string(level) + " " + raceName + " " + className + ")";
		m_presence.smallImageText = m_characterName.c_str();
		m_presence.smallImageKey = "mmorpgserver";
		Discord_UpdatePresence(&m_presence);
#endif
	}

	void Discord::NotifyZoneChanged(const String& zoneName)
	{
#ifdef MMO_WITH_DISCORD_RPC
		m_zoneName = zoneName;
		m_presence.details = m_zoneName.c_str();
		Discord_UpdatePresence(&m_presence);
#endif
	}

	void Discord::NotifyPartyChanged(bool hasParty, uint32 partySize, uint32 memberCount)
	{
#ifdef MMO_WITH_DISCORD_RPC
#endif
	}
}


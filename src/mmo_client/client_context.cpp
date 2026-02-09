#include "client_context.h"

#include "char_creation/char_create_info.h"
#include "char_creation/char_select.h"
#include "data/client_cache.h"
#include "discord.h"
#include "game_script.h"
#include "net/login_connector.h"
#include "net/realm_connector.h"
#include "systems/action_bar.h"
#include "systems/cooldown_manager.h"
#include "systems/friend_client.h"
#include "systems/guild_client.h"
#include "systems/inventory_client.h"
#include "systems/loot_client.h"
#include "systems/party_info.h"
#include "systems/quest_client.h"
#include "systems/spell_cast.h"
#include "systems/talent_client.h"
#include "systems/trainer_client.h"
#include "systems/vendor_client.h"
#include "ui/minimap.h"
#include "base/timer_queue.h"
#include "shared/audio/audio.h"

namespace mmo
{
	ClientContext::~ClientContext() = default;

	ClientContext& GetClientContext()
	{
		static ClientContext context;
		return context;
	}
}

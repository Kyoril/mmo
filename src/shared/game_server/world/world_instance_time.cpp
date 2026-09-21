// Implementation of HasPlayers and BroadcastGameTime methods for WorldInstance class

#include "world_instance.h"
#include "visibility_grid.h"
#include "visibility_tile.h"
#include "tile_subscriber.h"
#include "each_tile_in_sight.h"
#include "vector_sink.h"
#include "log/default_log_levels.h"
#include "game/object_type_id.h"
#include "game_server/objects/game_object_s.h"

#include <set>

namespace mmo
{
    bool WorldInstance::HasPlayers() const
    {
        for (const auto& [guid, object] : m_objectsByGuid)
        {
            if (object && object->GetTypeId() == ObjectTypeId::Player)
            {
                return true;
            }
        }
        return false;
    }

    void WorldInstance::BroadcastGameTime(const uint32 transitionMs)
    {
        // Get the current game time from the world instance
        const auto& gameTime = GetGameTime();

        // Prepare game time packet
        std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::GameTimeInfo);
        packet
            << io::write<GameTime>(gameTime.GetTime())
            << io::write<float>(gameTime.GetTimeSpeed())
            << io::write<uint32>(transitionMs);
        packet.Finish();

        // A player watches every tile in its sight range, so collect each one once: a transition
        // must start exactly once per client.
        std::set<TileSubscriber*> subscribers;
        const TileArea entireMap(TileIndex2D(0, 0), TileIndex2D(1024, 1024));
        ForEachTileInArea(*m_visibilityGrid, entireMap, [&subscribers](VisibilityTile& tile)
            {
                for (auto* subscriber : tile.GetWatchers())
                {
                    subscribers.insert(subscriber);
                }
            });

        for (auto* subscriber : subscribers)
        {
            subscriber->SendPacket(packet, buffer, true);
        }

        DLOG("Game time broadcast: " << m_gameTime.GetTimeString() << " (speed: " << m_gameTime.GetTimeSpeed() << "x, transition: " << transitionMs << " ms) to " << subscribers.size() << " players");
    }

    void WorldInstance::SetTimeOfDay(const GameTime timeOfDay, const uint32 transitionMs)
    {
        m_gameTime.SetTime(timeOfDay);
        BroadcastGameTime(transitionMs);
    }
}

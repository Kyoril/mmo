// Implementation of HasPlayers and BroadcastGameTime methods for WorldInstance class

#include "world_instance.h"
#include "visibility_grid.h"
#include "visibility_tile.h"
#include "tile_subscriber.h"
#include "each_tile_in_sight.h"
#include "vector_sink.h"
#include "log/default_log_levels.h"

namespace mmo
{
    bool WorldInstance::HasPlayers() const
    {
        // TODO
        return true;
    }

    void WorldInstance::BroadcastGameTime()
    {
        DLOG("Broadcasting game time...");

        // Get the current game time from the world instance
        const auto& gameTime = GetGameTime();

        // Prepare game time packet
        std::vector<char> buffer;
		io::VectorSink sink(buffer);
		game::OutgoingPacket packet(sink);
		packet.Start(game::realm_client_packet::GameTimeInfo);
        packet
            << io::write<GameTime>(gameTime.GetTime())
            << io::write<float>(gameTime.GetTimeSpeed());
        packet.Finish();

        // Get all tiles in the grid
        size_t counter = 0;
        const TileArea entireMap(TileIndex2D(0, 0), TileIndex2D(1024, 1024));
        ForEachTileInArea(*m_visibilityGrid, entireMap, [&packet, &buffer, &counter](VisibilityTile& tile)
            {
                for (auto* subscriber : tile.GetWatchers())
                {
                    // Need to cast to Player to send game time info
                    subscriber->SendPacket(packet, buffer, true);
                    counter++;
                }
            });

        DLOG("Game time broadcast: " << m_gameTime.GetTimeString() << " (speed: " << m_gameTime.GetTimeSpeed() << "x) to " << counter << " players"); 
    }
}

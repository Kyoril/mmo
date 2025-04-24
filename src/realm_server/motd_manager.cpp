#include "motd_manager.h"
#include "log/default_log_levels.h"

namespace mmo
{
    MOTDManager::MOTDManager(AsyncDatabase& database)
        : m_database(database)
        , m_motd("Welcome to the server!")  // Default MOTD until loaded from database
    {
        // Load the MOTD from the database on initialization
        LoadMessageOfTheDay();
    }

    void MOTDManager::LoadMessageOfTheDay()
    {
        // Create a handler that will update the MOTD when the async database request completes
        auto handler = [this](const std::optional<String>& motd) {
            if (motd)
            {
                ILOG("Message of the Day loaded: " << *motd);
                
                // Update the cached MOTD
                m_motd = *motd;
                
                // No need to fire signal here as this is initial loading
            }
            else
            {
                WLOG("Failed to load Message of the Day, using default");
            }
        };

        // Queue the async database request
        m_database.asyncRequest(std::move(handler), &IDatabase::GetMessageOfTheDay);
    }

    bool MOTDManager::SetMessageOfTheDay(const String& motd)
    {
        try
        {
            // Update the MOTD in the database asynchronously
            m_database.asyncRequest([this, motd](bool success) {
                if (success)
                {
                    // Update cached MOTD on success
                    ILOG("Message of the Day updated: " << motd);
                    m_motd = motd;
                    
                    // Fire the signal to notify subscribers that the MOTD has changed
                    motdChanged(motd);
                }
                else
                {
                    WLOG("Failed to update Message of the Day in the database");
                }
            }, &IDatabase::SetMessageOfTheDay, motd);

            return true;
        }
        catch (const std::exception& ex)
        {
            ELOG("Exception while updating Message of the Day: " << ex.what());
            return false;
        }
    }
}
#pragma once

#include "base/typedefs.h"
#include "database.h"
#include "base/signal.h"
#include <string>

namespace mmo
{
    // Forward declarations
    class AsyncDatabase;

    /// A class that manages the Message of the Day functionality
    class MOTDManager final
    {
    public:
        /// Initializes the MOTD manager with a database reference
        MOTDManager(AsyncDatabase& database);

        /// Loads the MOTD from the database asynchronously
        void LoadMessageOfTheDay();

        /// Returns the current MOTD
        const String& GetMessageOfTheDay() const { return m_motd; }

        /// Sets the MOTD and stores it in the database asynchronously
        /// @param motd The new message of the day
        /// @returns true if the operation was queued successfully, false if failed
        bool SetMessageOfTheDay(const String& motd);

    public:
        /// Signal that is fired when the MOTD has been updated
        /// @param motd The new message of the day
        signal<void(const String&)> motdChanged;

    private:
        /// Reference to the database
        AsyncDatabase& m_database;

        /// Currently cached MOTD
        String m_motd;
    };
}
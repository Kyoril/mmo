// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/signal.h"
#include "game/game.h"
#include "auth_protocol/auth_protocol.h"
#include "bug_report_rate_limiter.h"
#include "realm_subsystem_table.h"
#include <memory>
#include <mutex>
#include <list>
#include <optional>
#include <functional>

namespace mmo
{
	class Player;
	class MOTDManager;
	class ShutdownManager;

	/// Manages all connected players.
	///
	/// **Threading:** the realm server runs its io work on a single thread (see
	/// maxNetworkThreads in program.cpp), and this class is written for that. The mutex below
	/// guards the list against the database worker thread touching it; it does **not** make a
	/// returned Player* safe to hold. A raw pointer taken from here is valid only until the
	/// current handler returns, because the lifetime it points at is owned by this list.
	///
	/// Raising the realm server's thread count therefore requires converting these lookups to
	/// shared_ptr and routing cross-session calls through AbstractConnection::Post first -- the
	/// same change the login server received. Do not raise it without that. See
	/// docs/testing-servers.md.
	class PlayerManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Player>> Players;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit PlayerManager(
		    size_t playerCapacity,
			MOTDManager& motdManager
		);
		~PlayerManager();

		/// Notifies the manager that a player has been disconnected which will
		/// delete the player instance.
		void PlayerDisconnected(Player &player);

		/// Determines whether the player capacity limit has been reached.
		bool HasPlayerCapacityBeenReached();

		/// Gets the number of players currently connected to this realm.
		size_t GetPlayerCount() const;

		/// Adds a new player instance to the manager.
		void AddPlayer(std::shared_ptr<Player> added);

		/// Kicks a player by account id if connected.
		/// @param reason Forwarded to the client so it can explain the disconnect.
		void KickPlayerByAccountId(uint64 accountId, std::optional<auth::SessionKickReason> reason = std::nullopt);

		/// Kicks every session of an account except one.
		///
		/// The login server already broadcasts a kick when an account logs in again, so this is a
		/// local backstop for the two cases that broadcast cannot cover: this realm being
		/// disconnected from the login server when the broadcast went out, and a new session
		/// authenticating here before the broadcast arrives.
		///
		/// @param accountId The account whose other sessions are to be displaced.
		/// @param except The session to keep -- the one that has just authenticated.
		/// @param reason Forwarded to each displaced client so it can explain the disconnect.
		void KickOtherSessionsForAccount(uint64 accountId, const Player& except, auth::SessionKickReason reason);

		/// Gets a player by his account name.
		Player *GetPlayerByAccountName(const String &accountName);

		/// Gets a player by character guid.
		Player* GetPlayerByCharacterGuid(uint64 characterGuid);

		/// Gets a player by character name.
		Player* GetPlayerByCharacterName(const String& characterName);

		/// Gets the current Message of the Day from the MOTD manager.
		const String& GetMessageOfTheDay() const;

		/// Execute a function for each connected player.
		void ForEachPlayer(std::function<void(Player&)> callback) const;

		/// Disconnects every managed player. Used at shutdown so clients see a closed connection
		/// rather than a socket that simply stops answering.
		/// @param reason Sent to each client before the disconnect, if set.
		///
		/// Unlike the login server's equivalent this calls Kick() directly: the realm server runs
		/// all io work on one thread (see maxNetworkThreads in program.cpp), so the shutdown
		/// handler is already on the thread that owns every connection.
		void DisconnectAll(std::optional<auth::SessionKickReason> reason = std::nullopt);

		/// Broadcasts the Message of the Day to all connected players.
		void BroadcastMessageOfTheDay(const String& motd);

		/// Called when a world instance has been destroyed. Clears any dungeon bindings for that instance.
		/// @param instanceId The instance id that was destroyed.
		void OnInstanceDestroyed(InstanceId instanceId);

		/// Gets the per-character bug report rate limiter.
		BugReportRateLimiter& GetBugReportRateLimiter() { return m_bugReportRateLimiter; }

		/// Gets the realm's subsystem availability table.
		RealmSubsystemTable& GetSubsystemTable() { return m_subsystemTable; }

		/// Gets the realm's subsystem availability table.
		const RealmSubsystemTable& GetSubsystemTable() const { return m_subsystemTable; }

		/// Sets the name of this realm (reported with bug reports).
		void SetRealmName(String name) { m_realmName = std::move(name); }

		/// Gets the name of this realm.
		const String& GetRealmName() const { return m_realmName; }

		/// Sets the realm's shutdown manager (pending shutdowns are announced to entering players).
		void SetShutdownManager(ShutdownManager& shutdownManager) { m_shutdownManager = &shutdownManager; }

		/// Gets the realm's shutdown manager, or nullptr if none was set.
		ShutdownManager* GetShutdownManager() const { return m_shutdownManager; }

		/// Sends a shutdown countdown (remaining seconds or ShutdownCountdownCancelled) to every
		/// player that is in the world.
		void BroadcastShutdownCountdown(uint32 seconds);

	private:

		Players m_players;
		size_t m_playerCapacity;
		mutable std::mutex m_playerMutex;
		MOTDManager& m_motdManager;
		scoped_connection m_motdChangedConnection;
		BugReportRateLimiter m_bugReportRateLimiter;
		RealmSubsystemTable m_subsystemTable;
		String m_realmName;
		ShutdownManager* m_shutdownManager = nullptr;
	};
}

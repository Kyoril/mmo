// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_protocol/game_protocol.h"

#include "base/localization.h"

#include "tile_index.h"

namespace mmo
{
	class GameUnitS;
	class GameObjectS;

	class TileSubscriber
	{
	public:
		virtual ~TileSubscriber() = default;

	public:
		virtual GameUnitS& GetGameUnit() const = 0;

		/// Returns the client locale of the player behind this subscriber. Used to serve
		/// localized broadcast text (e.g. creature Say/Yell/Emote). Non-player subscribers
		/// and subscribers without a known locale default to the authoritative enUS base.
		virtual LocaleIndex GetLocale() const { return LocaleIndex::enUS; }

		virtual void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) = 0;

		/// Tells the subscriber that objects it did not know appear in its view.
		/// @param objects The objects that appear.
		/// @param reason game::spawn_reason::Spawned when the objects were just added to the world
		///        instance, CameIntoSight otherwise. Clients play a unit's spawn effect only for Spawned.
		virtual void NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects, game::spawn_reason::Type reason) = 0;

		/// Tells the subscriber that objects it knows disappear from its view.
		/// @param objects The objects that disappear.
		/// @param reason game::despawn_reason::Destroyed when the objects are removed from the world
		///        instance, OutOfSight when they still exist but can no longer be seen. Clients play a
		///        unit's despawn effect only for Destroyed.
		virtual void NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects, game::despawn_reason::Type reason) = 0;

		virtual GameTime ClientToServerTime(GameTime clientTimestamp) const = 0;

		virtual GameTime ServerToClientTime(GameTime serverTimestamp) const = 0;

		virtual bool HasReceivedTimeSyncResponse() const = 0;

		virtual void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, bool flush = true) = 0;

		/// Returns true if the client behind this subscriber has already received a creation
		/// packet for the given object GUID and has not yet received a destroy packet.
		/// Used to guard against sending spurious spawn or despawn packets.
		virtual bool IsObjectKnown(uint64 guid) const { return false; }

		/// Called when the per-observer visibility of a unit may have changed (e.g. stealth).
		/// The subscriber decides based on its own client state whether to spawn the unit,
		/// hide/show it via a UnitVisibilityList packet, despawn it or do nothing.
		virtual void NotifyUnitVisibilityChanged(GameUnitS& unit, bool visible) {}

		/// Returns true if the given object is known to the client but currently hidden
		/// from it (stealth). Hidden objects must not receive movement or field updates.
		virtual bool IsObjectHiddenForClient(uint64 guid) const { return false; }

		/// Returns false if this subscriber must be left out of a broadcast that starts at the given
		/// tile. A RemoteView says no for every tile its owner sees from its own tile already, so a
		/// client with two points of view is told about each event exactly once.
		virtual bool AcceptsBroadcastFrom(const TileIndex2D& sourceTile) const { return true; }

		/// Returns true if this subscriber is a second point of view of another subscriber (see
		/// RemoteView) rather than a client of its own. Broadcasts that collect every subscriber of a
		/// map skip these.
		virtual bool IsRemoteView() const { return false; }
	};
}

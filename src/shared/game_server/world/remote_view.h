// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tile_subscriber.h"
#include "tile_index.h"

#include "base/non_copyable.h"

namespace mmo
{
	class VisibilityGrid;
	class VisibilityTile;

	/// A second point of view of a tile subscriber, away from its own character: the camera of a
	/// cutscene. While it is attached to a tile, its owner is also sent everything in sight of that
	/// tile, on top of what the owner sees from the tile of its character.
	///
	/// The owner stays subscribed at its character the whole time. Nothing around the character is
	/// despawned for the client while the view travels, and the character itself can never leave its
	/// client's view. Where the two views overlap, the owner's own subscription wins: the view only
	/// passes on what the owner would not be told anyway, so no event reaches the client twice.
	///
	/// The owner has to return the favour in one place: it must not despawn an object that leaves
	/// its own sight while this view still sees it (see Sees).
	class RemoteView final : public TileSubscriber, public NonCopyable
	{
	public:
		/// @param owner The subscriber that gets a second point of view. Has to outlive this object.
		/// @param grid The grid of the world instance the owner's unit is in. Has to outlive this object.
		explicit RemoteView(TileSubscriber& owner, VisibilityGrid& grid);

		/// Detaches the view without telling the owner anything.
		~RemoteView() override;

	public:
		/// Whether the view currently watches a tile.
		[[nodiscard]] bool IsAttached() const { return m_attached; }

		/// The tile the view watches. Only meaningful while attached.
		[[nodiscard]] const TileIndex2D& GetTile() const { return m_tile; }

		/// Puts the view on a tile, or moves it there. The owner is sent the objects that come into
		/// sight, and a despawn for those that neither view sees any more.
		/// @param tile The tile to watch. Clamped to the grid.
		void MoveTo(const TileIndex2D& tile);

		/// Takes the view off the grid.
		/// @param notifyOwner true to despawn what only this view saw for the owner. false if the owner
		///        leaves the world anyway and its client forgets everything.
		void Detach(bool notifyOwner);

		/// Whether an object in the given tile is in sight of this view. The owner asks this before it
		/// despawns an object that left the sight of its own character.
		[[nodiscard]] bool Sees(const TileIndex2D& tile) const;

	public:
		/// @copydoc TileSubscriber::GetGameUnit
		GameUnitS& GetGameUnit() const override { return m_owner.GetGameUnit(); }

		/// @copydoc TileSubscriber::GetLocale
		LocaleIndex GetLocale() const override { return m_owner.GetLocale(); }

		/// @copydoc TileSubscriber::NotifyObjectsUpdated
		void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) override { m_owner.NotifyObjectsUpdated(objects); }

		/// @copydoc TileSubscriber::NotifyObjectsSpawned
		void NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects, game::spawn_reason::Type reason) override;

		/// @copydoc TileSubscriber::NotifyObjectsDespawned
		void NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects, game::despawn_reason::Type reason) override;

		/// @copydoc TileSubscriber::ClientToServerTime
		GameTime ClientToServerTime(const GameTime clientTimestamp) const override { return m_owner.ClientToServerTime(clientTimestamp); }

		/// @copydoc TileSubscriber::ServerToClientTime
		GameTime ServerToClientTime(const GameTime serverTimestamp) const override { return m_owner.ServerToClientTime(serverTimestamp); }

		/// @copydoc TileSubscriber::HasReceivedTimeSyncResponse
		bool HasReceivedTimeSyncResponse() const override { return m_owner.HasReceivedTimeSyncResponse(); }

		/// @copydoc TileSubscriber::SendPacket
		void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, const bool flush = true) override { m_owner.SendPacket(packet, buffer, flush); }

		/// @copydoc TileSubscriber::IsObjectKnown
		bool IsObjectKnown(const uint64 guid) const override { return m_owner.IsObjectKnown(guid); }

		/// @copydoc TileSubscriber::NotifyUnitVisibilityChanged
		void NotifyUnitVisibilityChanged(GameUnitS& unit, const bool visible) override { m_owner.NotifyUnitVisibilityChanged(unit, visible); }

		/// @copydoc TileSubscriber::IsObjectHiddenForClient
		bool IsObjectHiddenForClient(const uint64 guid) const override { return m_owner.IsObjectHiddenForClient(guid); }

		/// @copydoc TileSubscriber::AcceptsBroadcastFrom
		bool AcceptsBroadcastFrom(const TileIndex2D& sourceTile) const override;

		/// @copydoc TileSubscriber::IsRemoteView
		bool IsRemoteView() const override { return true; }

	private:
		/// Whether the owner sees the given tile from the tile of its character.
		bool OwnerSees(const TileIndex2D& tile) const;

		/// Sends the owner the objects of a tile it does not know yet.
		void SpawnTile(VisibilityTile& tile);

		/// Despawns the objects of a tile the owner knows.
		void DespawnTile(VisibilityTile& tile);

	private:
		TileSubscriber& m_owner;
		VisibilityGrid& m_grid;
		TileIndex2D m_tile;
		bool m_attached = false;
	};
}

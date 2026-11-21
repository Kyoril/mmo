// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "remote_view.h"

#include "each_tile_in_sight.h"
#include "visibility_grid.h"
#include "visibility_tile.h"

#include "objects/game_object_s.h"
#include "objects/game_unit_s.h"

namespace mmo
{
	RemoteView::RemoteView(TileSubscriber& owner, VisibilityGrid& grid)
		: m_owner(owner)
		, m_grid(grid)
	{
	}

	RemoteView::~RemoteView()
	{
		Detach(false);
	}

	void RemoteView::MoveTo(const TileIndex2D& tile)
	{
		VisibilityTile& newTile = m_grid.RequireTile(tile);
		const TileIndex2D newIndex = newTile.GetPosition();

		if (m_attached && newIndex == m_tile)
		{
			return;
		}

		const bool wasAttached = m_attached;
		const TileIndex2D oldIndex = m_tile;

		if (wasAttached)
		{
			m_grid.RequireTile(oldIndex).GetWatchers().optionalRemove(this);
		}

		// The view counts as standing on the new tile from here on, so that the owner agrees to
		// despawn what is left behind.
		m_tile = newIndex;
		m_attached = true;
		newTile.GetWatchers().add(this);

		if (wasAttached)
		{
			ForEachTileInSightWithout(m_grid, oldIndex, newIndex, [this](VisibilityTile& left)
			{
				if (!OwnerSees(left.GetPosition()))
				{
					DespawnTile(left);
				}
			});

			ForEachTileInSightWithout(m_grid, newIndex, oldIndex, [this](VisibilityTile& entered)
			{
				if (!OwnerSees(entered.GetPosition()))
				{
					SpawnTile(entered);
				}
			});
		}
		else
		{
			for (VisibilityTile* entered : GetTilesInSight(m_grid, newIndex))
			{
				if (!OwnerSees(entered->GetPosition()))
				{
					SpawnTile(*entered);
				}
			}
		}
	}

	void RemoteView::Detach(const bool notifyOwner)
	{
		if (!m_attached)
		{
			return;
		}

		m_attached = false;
		m_grid.RequireTile(m_tile).GetWatchers().optionalRemove(this);

		if (!notifyOwner)
		{
			return;
		}

		for (VisibilityTile* left : GetTilesInSight(m_grid, m_tile))
		{
			if (!OwnerSees(left->GetPosition()))
			{
				DespawnTile(*left);
			}
		}
	}

	bool RemoteView::Sees(const TileIndex2D& tile) const
	{
		return m_attached && IsInSight(m_tile, tile);
	}

	void RemoteView::NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects, const game::spawn_reason::Type reason)
	{
		// The owner may have been told through its own subscription already
		std::vector<GameObjectS*> unknown;
		unknown.reserve(objects.size());
		for (GameObjectS* object : objects)
		{
			if (!m_owner.IsObjectKnown(object->GetGuid()))
			{
				unknown.push_back(object);
			}
		}

		if (!unknown.empty())
		{
			m_owner.NotifyObjectsSpawned(unknown, reason);
		}
	}

	void RemoteView::NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects, const game::despawn_reason::Type reason)
	{
		std::vector<GameObjectS*> gone;
		gone.reserve(objects.size());
		for (GameObjectS* object : objects)
		{
			if (!m_owner.IsObjectKnown(object->GetGuid()))
			{
				continue;
			}

			// An object that only walked out of this view may still be in sight of the owner's character
			if (reason == game::despawn_reason::OutOfSight)
			{
				TileIndex2D objectTile;
				if (object->GetTileIndex(objectTile) && OwnerSees(objectTile))
				{
					continue;
				}
			}

			gone.push_back(object);
		}

		if (!gone.empty())
		{
			m_owner.NotifyObjectsDespawned(gone, reason);
		}
	}

	bool RemoteView::AcceptsBroadcastFrom(const TileIndex2D& sourceTile) const
	{
		return !OwnerSees(sourceTile);
	}

	bool RemoteView::OwnerSees(const TileIndex2D& tile) const
	{
		TileIndex2D ownerTile;
		return m_owner.GetGameUnit().GetTileIndex(ownerTile) && IsInSight(ownerTile, tile);
	}

	void RemoteView::SpawnTile(VisibilityTile& tile)
	{
		GameUnitS& viewer = m_owner.GetGameUnit();

		std::vector<GameObjectS*> objects;
		objects.reserve(tile.GetGameObjects().size());
		for (GameObjectS* object : tile.GetGameObjects())
		{
			if (object->GetGuid() == viewer.GetGuid() || m_owner.IsObjectKnown(object->GetGuid()))
			{
				continue;
			}

			if (object->IsUnit() && !object->AsUnit().CanBeSeenBy(viewer))
			{
				continue;
			}

			objects.push_back(object);
		}

		if (!objects.empty())
		{
			m_owner.NotifyObjectsSpawned(objects, game::spawn_reason::CameIntoSight);
		}
	}

	void RemoteView::DespawnTile(VisibilityTile& tile)
	{
		const uint64 viewerGuid = m_owner.GetGameUnit().GetGuid();

		std::vector<GameObjectS*> objects;
		objects.reserve(tile.GetGameObjects().size());
		for (GameObjectS* object : tile.GetGameObjects())
		{
			// The owner's own character is never taken from its client
			if (object->GetGuid() == viewerGuid || !m_owner.IsObjectKnown(object->GetGuid()))
			{
				continue;
			}

			objects.push_back(object);
		}

		if (!objects.empty())
		{
			m_owner.NotifyObjectsDespawned(objects, game::despawn_reason::OutOfSight);
		}
	}
}

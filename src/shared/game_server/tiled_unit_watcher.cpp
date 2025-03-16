
#include "tiled_unit_watcher.h"

#include "tiled_unit_finder_tile.h"

namespace mmo
{
	TiledUnitFinder::TiledUnitWatcher::TiledUnitWatcher(
		const Circle& shape,
		TiledUnitFinder& finder,
		UnitWatcher::VisibilityChange visibilityChanged)
		: UnitWatcher(shape, std::move(visibilityChanged))
		, m_finder(finder)
	{
	}

	TiledUnitFinder::TiledUnitWatcher::~TiledUnitWatcher()
	{
		for (auto& conn : m_connections)
		{
			conn.second.disconnect();
		}
	}

	void TiledUnitFinder::TiledUnitWatcher::Start()
	{
		m_previousShape = GetShape();

		const auto shapeArea = GetTileIndexArea(m_previousShape);
		ASSERT(shapeArea.topLeft[1] <= shapeArea.bottomRight[1]);
		ASSERT(shapeArea.topLeft[0] <= shapeArea.bottomRight[0]);

		for (TileIndex y = shapeArea.topLeft[1]; y <= shapeArea.bottomRight[1]; ++y)
		{
			for (TileIndex x = shapeArea.topLeft[0]; x <= shapeArea.bottomRight[0]; ++x)
			{
				auto& tile = m_finder.GetTile(TileIndex2D(x, y));
				if (WatchTile(tile))
				{
					//return;
				}
			}
		}

	}

	TileArea TiledUnitFinder::TiledUnitWatcher::GetTileIndexArea(const Circle& shape) const
	{
		const auto boundingBox = shape.GetBoundingRect();
		const auto topLeft = m_finder.GetTilePosition(boundingBox[1]);
		const auto bottomRight = m_finder.GetTilePosition(boundingBox[0]);
		return TileArea(topLeft, bottomRight);
	}

	bool TiledUnitFinder::TiledUnitWatcher::WatchTile(Tile& tile)
	{
		ASSERT(!m_connections.contains(&tile));

		const auto connection = tile.moved->connect(
			std::bind(&TiledUnitWatcher::OnUnitMoved,
				this, std::placeholders::_1));

		m_connections[&tile] = connection;

		for (GameUnitS* const unit : tile.GetUnits().getElements())
		{
			const Vector3& location = unit->GetPosition();

			if (GetShape().IsPointInside(Point(location.x, location.z)))
			{
				if (m_visibilityChanged(*unit, true))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TiledUnitFinder::TiledUnitWatcher::UnwatchTile(Tile& tile)
	{
		ASSERT(m_connections.contains(&tile));

		{
			const auto i = m_connections.find(&tile);
			ASSERT(i != m_connections.end());
			if (i != m_connections.end())
			{
				i->second.disconnect();
				m_connections.erase(i);
			}
		}

		for (GameUnitS* const unit : tile.GetUnits().getElements())
		{
			const Vector3& location = unit->GetPosition();
			if (GetShape().IsPointInside(Point(location.x, location.z)))
			{
				if (m_visibilityChanged(*unit, false))
				{
					return true;
				}
			}
		}

		return false;
	}

	void TiledUnitFinder::TiledUnitWatcher::OnUnitMoved(GameUnitS& unit)
	{
		const Vector3& location = unit.GetPosition();

		const bool isInside = GetShape().IsPointInside(Point(location.x, location.z));
		m_visibilityChanged(unit, isInside);
	}

	bool TiledUnitFinder::TiledUnitWatcher::UpdateTile(Tile& tile)
	{
		for (GameUnitS* const unit : tile.GetUnits().getElements())
		{
			const auto& location = unit->GetPosition();
			const auto planarPos = Point(location.x, location.z);
			const bool isInside = GetShape().IsPointInside(planarPos);

			if (m_visibilityChanged(*unit, isInside))
			{
				return true;
			}
		}

		return false;
	}

	void TiledUnitFinder::TiledUnitWatcher::OnShapeUpdated()
	{
		const auto previousArea = GetTileIndexArea(m_previousShape);

		m_previousShape = GetShape();
		const auto currentArea = GetTileIndexArea(m_previousShape);

		for (TileIndex y = previousArea.topLeft[1]; y <= previousArea.bottomRight[1]; ++y)
		{
			for (TileIndex x = previousArea.topLeft[0]; x <= previousArea.bottomRight[0]; ++x)
			{
				const TileIndex2D pos(x, y);
				auto& tile = m_finder.GetTile(pos);

				if (currentArea.IsInside(pos))
				{
					if (UpdateTile(tile))
					{
						//break;
					}
				}
				else
				{
					if (UnwatchTile(tile))
					{
						//break;
					}
				}
			}
		}

		for (TileIndex y = currentArea.topLeft[1]; y <= currentArea.bottomRight[1]; ++y)
		{
			for (TileIndex x = currentArea.topLeft[0]; x <= currentArea.bottomRight[0]; ++x)
			{
				const TileIndex2D pos(x, y);
				auto& tile = m_finder.GetTile(pos);

				if (previousArea.IsInside(pos))
				{
					if (UpdateTile(tile))
					{
						//break;
					}
				}
				else
				{
					if (WatchTile(tile))
					{
						//break;
					}
				}
			}
		}
	}
}

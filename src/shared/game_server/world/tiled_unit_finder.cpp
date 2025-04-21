
#include "tiled_unit_finder.h"

#include "game_server/objects/game_unit_s.h"
#include "tiled_unit_finder_tile.h"
#include "tiled_unit_watcher.h"

namespace mmo
{
	namespace
	{
		size_t GetFinderGridLength(float worldLength, float tileWidth)
		{
			return std::max<size_t>(1, static_cast<size_t>(worldLength / tileWidth) * 64);
		}

		inline Vector<Distance, 2> planar(const Vector3& point)
		{
			return Vector<Distance, 2>(
				point[0],
				point[2]);
		}
	}

	TiledUnitFinder::TiledUnitFinder(float tileWidth)
		: UnitFinder()
		, m_grid(GetFinderGridLength(533.33333f, tileWidth), GetFinderGridLength(533.33333f, tileWidth))
		, m_tileWidth(tileWidth)
	{
	}

	void TiledUnitFinder::AddUnit(GameUnitS& findable)
	{
		ASSERT(m_units.count(&findable) == 0);
		const Vector3& unitPos = findable.GetPosition();
		const auto position = GetTilePosition(planar(unitPos));
		auto& tile = m_grid(position[0], position[1]);

		if (!tile) {
			tile.reset(new Tile());
		}
		tile->AddUnit(findable);

		UnitRecord& record = *m_units.insert(std::make_pair(&findable, std::make_unique<UnitRecord>())).first->second;
		record.lastTile = tile.get();
	}

	void TiledUnitFinder::RemoveUnit(GameUnitS& findable)
	{
		const auto i = m_units.find(&findable);
		ASSERT(i != m_units.end());
		UnitRecord& record = *i->second;
		record.lastTile->RemoveUnit(findable);
		m_units.erase(i);
	}

	void TiledUnitFinder::UpdatePosition(GameUnitS& updated, const Vector3& previousPos)
	{
		//if (updated.GetPosition() != previousPos)
		{
			OnUnitMoved(updated);
		}
	}

	void TiledUnitFinder::FindUnits(
		const Circle& shape,
		const std::function<bool(GameUnitS&)>& resultHandler)
	{
		const auto boundingBox = shape.GetBoundingRect();
		auto topLeft = GetTilePosition(boundingBox[1]);
		auto bottomRight = GetTilePosition(boundingBox[0]);

		Tile::UnitSet iterationCopyTile;

		// Crash protection
		if (topLeft[0] < 0) {
			topLeft[0] = 0;
		}
		if (topLeft[1] < 0) {
			topLeft[1] = 0;
		}
		if (bottomRight[0] >= m_grid.width()) {
			bottomRight[0] = m_grid.width() - 1;
		}
		if (bottomRight[1] >= m_grid.height()) {
			bottomRight[1] = m_grid.height() - 1;
		}

		for (auto x = topLeft[0]; x <= bottomRight[0]; ++x)
		{
			for (auto y = topLeft[1]; y <= bottomRight[1]; ++y)
			{
				iterationCopyTile = GetTile(TileIndex2D(x, y)).GetUnits();

				for (GameUnitS* const element : iterationCopyTile.getElements())
				{
					ASSERT(element);
					const Vector3& elementPos = element->GetPosition();
					if (shape.IsPointInside(planar(elementPos)))
					{
						if (!resultHandler(*element))
						{
							return;
						}
					}
				}
			}
		}
	}

	std::unique_ptr<UnitWatcher> TiledUnitFinder::WatchUnits(const Circle& shape, std::function<bool(GameUnitS&, bool)> visibilityChanged)
	{
		return std::make_unique<TiledUnitWatcher>(shape, *this, std::move(visibilityChanged));
	}

	TiledUnitFinder::Tile& TiledUnitFinder::GetTile(const TileIndex2D& position)
	{
		auto& tile = m_grid(position[0], position[1]);
		if (!tile) {
			tile.reset(new Tile());
		}

		return *tile;
	}

	TileIndex2D TiledUnitFinder::GetTilePosition(const Vector<float, 2>& point) const
	{
		TileIndex2D output;

		// Calculate grid coordinates
		output[0] = static_cast<TileIndex>(floor((static_cast<double>(m_grid.width()) * 0.5 - floor(static_cast<double>(point[0]) / m_tileWidth))));
		output[1] = static_cast<TileIndex>(floor((static_cast<double>(m_grid.height()) * 0.5 - floor(static_cast<double>(point[1]) / m_tileWidth))));

		return output;
	}

	TiledUnitFinder::Tile& TiledUnitFinder::GetUnitsTile(const GameUnitS& findable)
	{
		const Vector3& position = findable.GetPosition();
		const TileIndex2D index = GetTilePosition(planar(position));
		return GetTile(index);
	}

	void TiledUnitFinder::OnUnitMoved(GameUnitS& findable)
	{
		Tile& currentTile = GetUnitsTile(findable);
		UnitRecord& record = RequireRecord(findable);
		if (&currentTile == record.lastTile)
		{
			(*currentTile.moved)(findable);
			return;
		}

		RemoveUnit(findable);
		AddUnit(findable);
	}

	TiledUnitFinder::UnitRecord& TiledUnitFinder::RequireRecord(GameUnitS& findable)
	{
		const auto i = m_units.find(&findable);
		ASSERT(i != m_units.end());
		UnitRecord& record = *i->second;
		return record;
	}
}

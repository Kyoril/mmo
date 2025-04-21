// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "tiled_unit_finder_tile.h"

namespace mmo
{
	TiledUnitFinder::Tile::Tile()
		: moved(new MoveSignal)
	{
	}

	TiledUnitFinder::Tile::Tile(Tile&& other)
	{
		swap(other);
	}

	TiledUnitFinder::Tile& TiledUnitFinder::Tile::operator=(Tile&& other)
	{
		swap(other);
		return *this;
	}

	void TiledUnitFinder::Tile::swap(Tile& other)
	{
		moved.swap(other.moved);
		m_units.swap(other.m_units);
	}

	const TiledUnitFinder::Tile::UnitSet& TiledUnitFinder::Tile::GetUnits() const
	{
		return m_units;
	}

	void TiledUnitFinder::Tile::AddUnit(GameUnitS& unit)
	{
		m_units.add(&unit);
		(*moved)(unit);
	}

	void TiledUnitFinder::Tile::RemoveUnit(GameUnitS& unit)
	{
		m_units.remove(&unit);
	}
}
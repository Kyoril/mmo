// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "tiled_unit_finder.h"
#include "base/linear_set.h"

namespace mmo
{
	class TiledUnitFinder::Tile final
	{
	public:

		typedef LinearSet<GameUnitS*> UnitSet;
		typedef signal<void(GameUnitS&)> MoveSignal;

		//unique_ptr, so that Tile is movable
		std::unique_ptr<MoveSignal> moved;

		Tile();
		Tile(Tile&& other);

		Tile& operator=(Tile&& other);
		void swap(Tile& other);
		const UnitSet& GetUnits() const;
		void AddUnit(GameUnitS& unit);
		void RemoveUnit(GameUnitS& unit);

	private:

		UnitSet m_units;
	};
}

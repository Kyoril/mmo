// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "tile_index.h"
#include "base/linear_set.h"

namespace mmo
{
	class GameObject;
	class TileSubscriber;

	class VisibilityTile
	{
	public:
		typedef LinearSet<GameObject *> GameObjects;
		typedef LinearSet<TileSubscriber *> Watchers;

	public:
		explicit VisibilityTile() = default;
		virtual ~VisibilityTile() = default;

	public:
		void SetPosition(const TileIndex2D &position) { m_position = position; }

		const TileIndex2D &GetPosition() const { return m_position; }

		GameObjects &GetGameObjects() { return m_objects; }

		const GameObjects &GetGameObjects() const { return m_objects; }

		Watchers &GetWatchers() { return m_watchers; }

		const Watchers &GetWatchers() const { return m_watchers; }

	private:

		TileIndex2D m_position;
		GameObjects m_objects;
		Watchers m_watchers;
	};
}

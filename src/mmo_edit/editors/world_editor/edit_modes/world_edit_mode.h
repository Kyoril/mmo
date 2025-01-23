// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	namespace proto
	{
		class UnitSpawnEntry;
	}

	class IWorldEditor
	{
	public:
		virtual ~IWorldEditor() = default;

	public:
		virtual void ClearSelection() = 0;

		virtual void RemoveAllUnitSpawns() = 0;

		virtual void AddUnitSpawn(proto::UnitSpawnEntry& spawn, bool select) = 0;
	};

	class WorldEditMode : public NonCopyable
	{
	public:
		explicit WorldEditMode(IWorldEditor& worldEditor);
		virtual ~WorldEditMode() = default;

	public:
		virtual const char* GetName() const = 0;

		virtual void DrawDetails() = 0;

		virtual void OnActivate() {}

		virtual void OnDeactivate() {}

		virtual void OnMouseDown(float x, float y) {}

		virtual void OnMouseMoved(float x, float y) {}

		virtual void OnMouseUp(float x, float y) {}

		virtual void OnMouseHold(float deltaSeconds) {}

	protected:
		IWorldEditor& m_worldEditor;
	};
}

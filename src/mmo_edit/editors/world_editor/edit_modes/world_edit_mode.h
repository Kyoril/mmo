// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	class Quaternion;
	class Vector3;
	class Entity;
	class Camera;

	namespace proto
	{
		class ObjectSpawnEntry;
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

		virtual void AddObjectSpawn(proto::ObjectSpawnEntry& spawn) = 0;

		virtual Camera& GetCamera() const = 0;

		virtual bool IsGridSnapEnabled() const = 0;

		virtual float GetTranslateGridSnapSize() const = 0;

		virtual Entity* CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 objectId) = 0;

		virtual bool HasTerrain() const = 0;

		virtual terrain::Terrain* GetTerrain() const = 0;
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

		virtual bool SupportsViewportDrop() const { return false; }

		virtual void OnViewportDrop(float x, float y) {}

	protected:
		IWorldEditor& m_worldEditor;
	};
}

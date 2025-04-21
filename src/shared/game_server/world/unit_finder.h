#pragma once

#include "base/non_copyable.h"
#include "math/vector3.h"
#include "game/circle.h"

#include <functional>
#include <memory>

namespace mmo
{
	class GameUnitS;
	class UnitWatcher;

	class UnitFinder : public NonCopyable
	{
	public:
		UnitFinder() = default;
		virtual ~UnitFinder() = default;

	public:
		/// @param findable
		virtual void AddUnit(GameUnitS& findable) = 0;

		/// @param findable
		virtual void RemoveUnit(GameUnitS& findable) = 0;

		/// @param updated
		/// @param previousPos
		virtual void UpdatePosition(GameUnitS& updated, const Vector3& previousPos) = 0;

		/// @param shape
		/// @param resultHandler
		virtual void FindUnits(const Circle& shape, const std::function<bool(GameUnitS&)>& resultHandler) = 0;

		/// @param shape
		virtual std::unique_ptr<UnitWatcher> WatchUnits(const Circle& shape, std::function<bool(GameUnitS&, bool)> visibilityChanged) = 0;
	};
}

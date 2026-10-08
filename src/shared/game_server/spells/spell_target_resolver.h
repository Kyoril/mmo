// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/proto_data/spells.pb.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <memory>
#include <vector>

namespace mmo
{
	class SpellCastContext;
	class GameObjectS;
	class GameUnitS;

	/// Width of a ConeEnemy effect whose miscvalueb leaves it unset, in degrees.
	constexpr float DefaultConeDegrees = 90.0f;

	/// Tests whether a position lies inside a horizontal cone. The height difference is ignored.
	/// @param origin Apex of the cone.
	/// @param facing Direction the cone opens along, in the engine's facing convention.
	/// @param halfConeRadians Half of the cone's full width.
	/// @param position Position to test.
	/// @return True if the position is inside the cone, or on its apex.
	bool IsInPlanarCone(const Vector3& origin, const Radian& facing, float halfConeRadians, const Vector3& position);

	class SpellTargetResolver final
	{
	public:
		explicit SpellTargetResolver(const SpellCastContext& context);

		GameUnitS* ResolveUnitTarget() const;
		std::shared_ptr<GameUnitS> ResolveEffectUnitTarget(const proto::SpellEffect& effect) const;
		bool ResolveEffectTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;

	private:
		void PrepareTargetsBuffer(std::vector<GameObjectS*>& targets) const;
		bool ResolveSingleCasterTarget(std::vector<GameObjectS*>& targets) const;
		bool ResolveSingleObjectTarget(std::vector<GameObjectS*>& targets) const;
		bool ResolveSingleUnitTarget(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;
		bool ResolvePartyOrNearbyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;
		bool ResolveAreaEnemyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;
		bool ResolveConeEnemyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;
		bool ResolveSecondaryEnemyTargets(const proto::SpellEffect& effect, std::vector<GameObjectS*>& targets) const;
		bool CollectEnemyUnitsInRadius(const proto::SpellEffect& effect, float centerX, float centerZ, std::vector<GameObjectS*>& targets) const;
		bool CanAddUnitTarget(const std::vector<GameObjectS*>& targets, const GameUnitS& unit) const;
		bool ValidateEffectRadius(const proto::SpellEffect& effect) const;

		const SpellCastContext& m_context;
	};
}

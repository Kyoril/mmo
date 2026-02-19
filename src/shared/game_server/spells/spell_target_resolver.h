// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "shared/proto_data/spells.pb.h"

#include <memory>
#include <vector>

namespace mmo
{
	class SpellCastContext;
	class GameObjectS;
	class GameUnitS;

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
		bool CollectEnemyUnitsInRadius(const proto::SpellEffect& effect, float centerX, float centerZ, std::vector<GameObjectS*>& targets) const;
		bool CanAddUnitTarget(const std::vector<GameObjectS*>& targets, const GameUnitS& unit) const;
		bool ValidateEffectRadius(const proto::SpellEffect& effect) const;

		const SpellCastContext& m_context;
	};
}

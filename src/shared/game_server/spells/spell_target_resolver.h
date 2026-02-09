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
		const SpellCastContext& m_context;
	};
}

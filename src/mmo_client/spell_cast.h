// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/signal.h"
#include "client_data/project.h"
#include "game/spell_target_map.h"

namespace mmo
{
	class RealmConnector;

	/// This class allows for spell casting support.
	class SpellCast final : public NonCopyable
	{
	public:
		signal<void()> SpellCastStarted;
		signal<void(bool /*cancelled*/)> SpellCastEnded;
		signal<void()> SpellCastTargetRequired;

	public:
		SpellCast(RealmConnector& connector, const proto_client::SpellManager& spells, const proto_client::RangeManager& ranges);
		~SpellCast() override = default;

	public:
		void OnEnterWorld();

		void OnLeftWorld();

		void OnSpellStart(const proto_client::SpellEntry& spell, GameTime castTime);

		void OnSpellGo(uint32 spellId);

		void OnSpellFailure(uint32 spellId);

		bool SetSpellTargetMap(SpellTargetMap& targetMap, const proto_client::SpellEntry& spell);

	public:
		void CastSpell(uint32 spellId);

		bool CancelCast();

		bool IsCasting() const;

		uint32 GetCastingSpellId() const;

	private:
		RealmConnector& m_connector;

		const proto_client::SpellManager& m_spells;

		const proto_client::RangeManager& m_ranges;

		uint32 m_spellCastId = 0;


	};
}

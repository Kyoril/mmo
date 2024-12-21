// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/signal.h"
#include "client_data/project.h"

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
		SpellCast(RealmConnector& connector, const proto_client::SpellManager& spells);
		~SpellCast() override = default;

	public:
		void OnEnterWorld();

		void OnLeftWorld();

		void OnSpellGo(uint32 spellId);

		void OnSpellFailure(uint32 spellId);

	public:
		void CastSpell(uint32 spellId);

		void CancelCast();

		bool IsCasting() const;

		uint32 GetCastingSpellId() const;

	private:
		RealmConnector& m_connector;

		const proto_client::SpellManager& m_spells;

		uint32 m_spellCastId = 0;
	};
}

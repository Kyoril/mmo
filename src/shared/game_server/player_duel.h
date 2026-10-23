// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once

#include "base/clock.h"
#include "base/signal.h"
#include "game/duel.h"
#include "math/vector3.h"
#include <functional>
#include <memory>

namespace mmo
{
	class GameUnitS;

	/// Server-owned two-player duel, with a deterministic clock for countdown and invite expiry.
	class PlayerDuel final : public std::enable_shared_from_this<PlayerDuel>
	{
	public:
		using Notify = std::function<void(uint64, DuelEvent, uint8, const String&, const Vector3&)>;
		/// Creates an invitation. Call Start after storing the shared session on both players.
		PlayerDuel(std::shared_ptr<GameUnitS> challenger, std::shared_ptr<GameUnitS> challenged, Notify notify, GameTime now);
		/// Announces the request and connects the health limit to the duel outcome.
		void Start();
		/// Only the challenged player may accept. Revalidates health, distance and world.
		void Accept(uint64 player, GameTime now);
		/// Declines a pending invitation, or cancels/forfeits an existing duel.
		void Cancel(uint64 player, DuelResult reason = DuelResult::Cancelled);
		/// Advances countdown/expiry and checks that both participants remain available.
		void Update(GameTime now);
		/// Whether cleanup and outcome notifications have already run.
		bool IsFinished() const { return m_finished; }
	private:
		bool IsValid() const;
		void Send(DuelEvent event, uint8 first = 0, uint8 second = 0);
		void Finish(uint64 winner, DuelResult reason);
		std::weak_ptr<GameUnitS> m_players[2];
		uint64 m_guids[2];
		String m_names[2];
		Notify m_notify;
		Vector3 m_flag;
		GameTime m_deadline;
		bool m_accepted{false}, m_active{false}, m_finished{false};
		uint8 m_lastCount{0};
		scoped_connection_container m_connections;
	};
}

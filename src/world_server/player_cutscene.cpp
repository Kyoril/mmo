// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The cutscene of a world node player: starting and ending it, and moving the player's second point
// of view along the camera path while the client plays it. See docs/cutscenes.md.

#include "player.h"

#include "base/utilities.h"
#include "game/cutscene.h"
#include "game_server/objects/game_player_s.h"
#include "game_server/world/remote_view.h"
#include "game_server/world/world_instance.h"
#include "log/default_log_levels.h"
#include "shared/proto_data/project.h"

namespace mmo
{
	namespace
	{
		/// How long the client may take to report that it started playing: it may still be on its
		/// loading screen when the cutscene is announced.
		constexpr GameTime CutsceneBeginTimeout = constants::OneMinute * 2;

		/// How often the point of view is moved along the camera path.
		constexpr GameTime CutsceneTickInterval = 200;

		/// How much longer than its length a cutscene may run before the server ends it itself. Covers
		/// a client that plays a little slower than the server counts.
		constexpr GameTime CutsceneEndGrace = constants::OneSecond * 10;

		/// A client may report the natural end of a cutscene this much before the server reaches it.
		constexpr GameTime CutsceneEarlyEndTolerance = constants::OneSecond * 3;

		/// How far the character may end up from where it stood when the cutscene was announced: it
		/// may finish the step it was taking, or settle onto the ground after entering the world.
		constexpr float CutsceneMovementTolerance = 4.0f;

		/// How long after a cutscene the next one has to wait. A cutscene protects the character, and
		/// a trigger that can be walked into again and again must not hand that out without a pause.
		constexpr GameTime CutsceneCooldown = constants::OneSecond * 30;
	}

	bool Player::StartCutscene(const uint32 cutsceneId, const bool force)
	{
		if (!m_worldInstance || !m_character)
		{
			return false;
		}

		const proto::CutsceneEntry* cutscene = m_project.cutscenes.getById(cutsceneId);
		if (!cutscene)
		{
			WLOG("Cutscene " << cutsceneId << " does not exist");
			return false;
		}

		if (!IsCutscenePlayable(*cutscene))
		{
			WLOG("Cutscene " << cutsceneId << " has no shot with a length and cannot be played");
			return false;
		}

		if (cutscene->map() != m_worldInstance->GetMapId())
		{
			WLOG("Cutscene " << cutsceneId << " plays on map " << cutscene->map() << ", but the character is on map " << m_worldInstance->GetMapId());
			return false;
		}

		// One at a time, and never for a character that has to fight or is dead
		if (m_cutscene || !m_character->IsAlive() || m_character->IsInCombat())
		{
			return false;
		}

		const GameTime now = GetAsyncTimeMs();
		if (!force && now < m_cutsceneCooldownEnd)
		{
			return false;
		}

		m_cutscene = cutscene;
		m_cutsceneRequestTime = now;
		m_cutsceneStartTime = 0;
		m_cutsceneStartPosition = m_character->GetPosition();

		// The character stands still and is left alone until the cutscene is over
		m_character->StopAttack();
		m_character->CancelCast(spell_interrupt_flags::Any);
		CloseLootDialog();
		m_character->AddFlag<uint32>(object_fields::Flags, unit_flags::InCutscene);

		SendPacket([cutsceneId](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::CutsceneStart);
			packet << io::write<uint32>(cutsceneId);
			packet.Finish();
		});

		// The path is followed once the client reports that it plays
		m_cutsceneTick.SetEnd(m_cutsceneRequestTime + CutsceneBeginTimeout);
		return true;
	}

	void Player::EndCutscene(const cutscene_end_reason::Type reason)
	{
		if (!m_cutscene)
		{
			return;
		}

		const uint32 cutsceneId = m_cutscene->id();
		ResetCutsceneState(true);

		SendPacket([cutsceneId, reason](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::CutsceneEnd);
			packet << io::write<uint32>(cutsceneId) << io::write<uint8>(reason);
			packet.Finish();
		});
	}

	void Player::ResetCutsceneState(const bool notifyClient)
	{
		m_cutsceneTick.Cancel();
		if (m_cutscene)
		{
			m_cutsceneCooldownEnd = GetAsyncTimeMs() + CutsceneCooldown;
		}
		m_cutscene = nullptr;
		m_cutsceneStartTime = 0;

		if (m_cutsceneView)
		{
			m_cutsceneView->Detach(notifyClient);
			m_cutsceneView.reset();
		}

		if (m_character)
		{
			m_character->RemoveFlag<uint32>(object_fields::Flags, unit_flags::InCutscene);
		}
	}

	void Player::OnCutsceneTick()
	{
		if (!m_cutscene)
		{
			return;
		}

		// The client never started playing
		if (m_cutsceneStartTime == 0)
		{
			WLOG("Client of " << log_hex_digit(m_character->GetGuid()) << " did not start cutscene " << m_cutscene->id() << " in time");
			EndCutscene(cutscene_end_reason::Cancelled);
			return;
		}

		const GameTime now = GetAsyncTimeMs();
		const GameTime elapsed = now - m_cutsceneStartTime;
		if (elapsed > GetCutsceneDuration(*m_cutscene) + CutsceneEndGrace)
		{
			EndCutscene(cutscene_end_reason::Finished);
			return;
		}

		UpdateCutsceneView(elapsed);
		m_cutsceneTick.SetEnd(now + CutsceneTickInterval);
	}

	void Player::UpdateCutsceneView(const GameTime elapsed)
	{
		if (!m_cutscene || !m_worldInstance)
		{
			return;
		}

		CutsceneSample sample;
		if (!SampleCutscene(*m_cutscene, static_cast<float>(elapsed), sample))
		{
			return;
		}

		if (!m_cutsceneView)
		{
			m_cutsceneView = std::make_unique<RemoteView>(*this, m_worldInstance->GetGrid());
		}

		// A camera outside the grid watches from the nearest tile inside it
		TileIndex2D tile;
		m_worldInstance->GetGrid().GetTilePosition(sample.position, tile[0], tile[1]);
		m_cutsceneView->MoveTo(tile);
	}

	void Player::OnCutsceneBegin(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 cutsceneId = 0;
		if (!(contentReader >> io::read<uint32>(cutsceneId)))
		{
			WLOG("Could not read packet data");
			return;
		}

		if (!m_cutscene || m_cutscene->id() != cutsceneId || m_cutsceneStartTime != 0)
		{
			return;
		}

		m_cutsceneStartTime = GetAsyncTimeMs();
		UpdateCutsceneView(0);
		m_cutsceneTick.SetEnd(m_cutsceneStartTime + CutsceneTickInterval);
	}

	void Player::OnCutsceneEnd(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 cutsceneId = 0;
		uint8 skipped = 0;
		if (!(contentReader >> io::read<uint32>(cutsceneId) >> io::read<uint8>(skipped)))
		{
			WLOG("Could not read packet data");
			return;
		}

		if (!m_cutscene || m_cutscene->id() != cutsceneId)
		{
			return;
		}

		if ((m_cutscene->flags() & cutscene_flags::Skippable) != 0)
		{
			EndCutscene(skipped != 0 ? cutscene_end_reason::Skipped : cutscene_end_reason::Finished);
			return;
		}

		// A cutscene that may not be skipped only ends when it has (nearly) played through. Anything
		// earlier is ignored, and the cutscene ends on the server's own clock.
		const GameTime duration = GetCutsceneDuration(*m_cutscene);
		const GameTime elapsed = m_cutsceneStartTime != 0 ? GetAsyncTimeMs() - m_cutsceneStartTime : 0;
		if (m_cutsceneStartTime != 0 && elapsed + CutsceneEarlyEndTolerance >= duration)
		{
			EndCutscene(cutscene_end_reason::Finished);
		}
	}

	bool Player::IsCutsceneMovementViolation(const MovementInfo& info) const
	{
		if (!m_cutscene)
		{
			return false;
		}

		// Judged by where the character ends up, not by movement flags: a jump or a fall changes the
		// position without any of them.
		const float dx = info.position.x - m_cutsceneStartPosition.x;
		const float dz = info.position.z - m_cutsceneStartPosition.z;
		return dx * dx + dz * dz > CutsceneMovementTolerance * CutsceneMovementTolerance;
	}

#if MMO_WITH_DEV_COMMANDS
	void Player::OnCheatPlayCutscene(uint16 opCode, uint32 size, io::Reader& contentReader)
	{
		uint32 cutsceneId = 0;
		if (!(contentReader >> io::read<uint32>(cutsceneId)))
		{
			ELOG("Missing cutscene id");
			return;
		}

		DLOG("Playing cutscene " << cutsceneId << " (GM)");

		// Asking again while one runs ends the running one first, so a GM can iterate on a path
		EndCutscene(cutscene_end_reason::Cancelled);
		StartCutscene(cutsceneId, true);
	}
#endif
}

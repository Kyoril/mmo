// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
//
// The world state's side of cutscenes: it gives the camera, the ui and the player's input to the
// CutscenePlayer while a cutscene plays and takes them back afterwards, and it makes sure the world
// is loaded where the camera cuts to. See docs/cutscenes.md.

#include "world_state.h"

#include "loading_screen.h"
#include "player_controller.h"
#include "systems/cutscene_player.h"

#include "frame_ui/frame_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"

namespace mmo
{
	bool WorldState::IsCutsceneActive() const
	{
		return m_cutscenePlayer && (m_cutscenePlayer->IsPlaying() || m_cutscenePlayer->HasPending());
	}

	bool WorldState::IsWorldLoadedAround(const Vector3& position) const
	{
		// The world is loaded three pages wide around the camera at least
		const PagePosition cameraPage = GetPagePositionFromCamera();
		const int32 pageX = static_cast<int32>(GetTerrainPageIndex(position.x));
		const int32 pageZ = static_cast<int32>(GetTerrainPageIndex(position.z));

		return std::abs(pageX - static_cast<int32>(cameraPage.x())) <= 1 &&
			std::abs(pageZ - static_cast<int32>(cameraPage.y())) <= 1;
	}

	void WorldState::UpdateCutscene(const float deltaSeconds)
	{
		if (!m_cutscenePlayer || !m_playerController)
		{
			return;
		}

		const bool worldReady = m_worldLoaded && m_timeSyncResponseSent && m_playerController->GetControlledUnit();

		if (m_cutscenePlayer->HasPending() && worldReady)
		{
			BeginPendingCutscene();
		}
		else if (m_cutscenePlayer->IsPlaying())
		{
			m_cutscenePlayer->Update(deltaSeconds);
			ApplyCutsceneView();
		}

		// The loading screen was kept up for a cutscene that never began (the server took it back)
		if (m_cutsceneHoldsLoadingScreen && !IsCutsceneActive())
		{
			m_cutsceneHoldsLoadingScreen = false;
			LoadingScreen::Hide();
		}
	}

	void WorldState::BeginPendingCutscene()
	{
		const proto_client::CutsceneEntry* cutscene = m_cutscenePlayer->GetPending();
		ASSERT(cutscene);

		// A cutscene that opens somewhere else is loaded behind the loading screen, so that its first
		// picture is the finished world and not terrain streaming in.
		CutscenePlayer::View start;
		const bool hasStart = CutscenePlayer::GetStartView(*cutscene, start);
		const bool loadFirst = hasStart && start.weight >= 1.0f && !IsWorldLoadedAround(start.position);
		if (loadFirst)
		{
			LoadingScreen::Show();
			m_playerController->SetCameraOverride(start.position, start.orientation, 1.0f, start.fov);
			LoadWorldAround(start.position);
		}

		m_cutsceneShot = 0;
		m_cutscenePlayer->Begin();
		ApplyCutsceneView();

		if (loadFirst || m_cutsceneHoldsLoadingScreen)
		{
			m_cutsceneHoldsLoadingScreen = false;
			LoadingScreen::Hide();
		}
	}

	void WorldState::ApplyCutsceneView()
	{
		CutscenePlayer::View view;
		if (!m_cutscenePlayer->GetView(view))
		{
			return;
		}

		// A cut to a camera far away: load the world there before showing it
		if (view.shot != m_cutsceneShot)
		{
			m_cutsceneShot = view.shot;

			if (view.weight >= 1.0f && !IsWorldLoadedAround(view.position))
			{
				LoadingScreen::Show();
				m_playerController->SetCameraOverride(view.position, view.orientation, 1.0f, view.fov);
				LoadWorldAround(view.position);
				LoadingScreen::Hide();
			}
		}

		m_playerController->SetCameraOverride(view.position, view.orientation, view.weight, view.fov);
	}

	void WorldState::OnCutsceneStarted(const proto_client::CutsceneEntry& cutscene)
	{
		m_playerController->SetInputLocked(true);
		FrameManager::Get().SetUiHidden(true);

		if (cutscene.music() != 0)
		{
			m_cutsceneMusic = true;
			m_zoneMusic.SetSound(cutscene.music());
		}

		FrameManager::Get().TriggerLuaEvent("CUTSCENE_STARTED", cutscene.id());
	}

	void WorldState::OnCutsceneEnded(const proto_client::CutsceneEntry& cutscene, const cutscene_end_reason::Type reason)
	{
		// The camera returns to the character. If the cutscene left off far away, the world around
		// the character has been unloaded in the meantime. Measured before the camera moves back:
		// the loaded pages are the ones around the last picture of the cutscene.
		const auto unit = m_playerController->GetControlledUnit();
		const bool reload = unit && m_worldLoaded && !IsWorldLoadedAround(unit->GetPosition());

		RestoreAfterCutscene();

		if (reload)
		{
			LoadingScreen::Show();
			LoadWorldAround(unit->GetPosition());
			LoadingScreen::Hide();
		}

		FrameManager::Get().TriggerLuaEvent("CUTSCENE_ENDED", cutscene.id(), static_cast<int32>(reason));
	}

	void WorldState::RestoreAfterCutscene()
	{
		if (m_playerController)
		{
			m_playerController->ClearCameraOverride();
			m_playerController->SetInputLocked(false);
		}

		FrameManager::Get().SetUiHidden(false);

		if (m_cutsceneMusic)
		{
			m_cutsceneMusic = false;
			m_zoneMusic.SetSound(ResolveZoneSound(m_project.zones.getById(m_lastZoneId), true));
		}
	}

	void WorldState::AbortCutscene()
	{
		if (m_cutscenePlayer)
		{
			m_cutscenePlayer->Reset();
		}

		RestoreAfterCutscene();

		if (m_cutsceneHoldsLoadingScreen)
		{
			m_cutsceneHoldsLoadingScreen = false;
		}
	}

	PacketParseResult WorldState::OnCutsceneStart(game::IncomingPacket& packet)
	{
		uint32 cutsceneId = 0;
		if (!(packet >> io::read<uint32>(cutsceneId)))
		{
			ELOG("Failed to read CutsceneStart packet!");
			return PacketParseResult::Disconnect;
		}

		if (m_cutscenePlayer)
		{
			m_cutscenePlayer->Request(cutsceneId);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult WorldState::OnCutsceneEnd(game::IncomingPacket& packet)
	{
		uint32 cutsceneId = 0;
		uint8 reason = 0;
		if (!(packet >> io::read<uint32>(cutsceneId) >> io::read<uint8>(reason)))
		{
			ELOG("Failed to read CutsceneEnd packet!");
			return PacketParseResult::Disconnect;
		}

		if (m_cutscenePlayer && reason < cutscene_end_reason::Count_)
		{
			m_cutscenePlayer->OnServerEnded(cutsceneId, static_cast<cutscene_end_reason::Type>(reason));
		}

		return PacketParseResult::Pass;
	}

	void WorldState::Command_Cutscene(const std::string& cmd, const std::string& args) const
	{
		const uint32 cutsceneId = static_cast<uint32>(std::strtoul(args.c_str(), nullptr, 10));
		if (cutsceneId == 0)
		{
			ELOG("Usage: cutscene <cutsceneId>");
			return;
		}

		m_realmConnector.PlayCutscene(cutsceneId);
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene.h"
#include "plaza_avatars.h"

#include "environment_apply.h"
#include "world_deserializer.h"

#include "assets/asset_registry.h"
#include "base/clock.h"
#include "base/profiler.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "console/console_var.h"
#include "graphics/sky_component.h"
#include "log/default_log_levels.h"
#include "math/constants.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mmo
{
	namespace
	{
		/// Milliseconds of queued streaming work finished per frame.
		constexpr double dispatchBudgetMs = 4.0;

		/// Half the width of the idle drift at the gate, in metres.
		constexpr float driftAmplitude = 0.3f;

		/// Seconds of one idle drift cycle at the gate.
		constexpr float driftPeriodSeconds = 20.0f;

		/// Share of the way from the plaza camera to the selected character covered by the push-in.
		constexpr float pushInFraction = 0.4f;

		/// Farthest a cursor ray reaches, in metres.
		constexpr float pickDistance = 200.0f;

		/// Height above the character's feet the push-in moves towards, in metres (about chest height).
		constexpr float pushInTargetHeight = 1.4f;

		/// Milliseconds after loading of the gate (resp. the plaza) began when it counts as ready
		/// even if it is not loaded, so neither the fade nor the flight input lock lasts forever.
		constexpr GameTime readinessTimeoutMs = 8000;

		/// Overrides the camera path parameter while >= 0, to inspect points of the flight path.
		ConsoleVar* s_glueCameraPathVar = nullptr;
	}

	GlueScene::GlueScene(const proto_client::Project& project, GlueSceneDefinition definition, SoundEntryPlayer& soundEntryPlayer, IAudio& audio)
		: m_project(project)
		, m_definition(std::move(definition))
		, m_director(m_definition.flightSeconds)
		, m_environmentProfiles(project.environmentProfiles)
		, m_ambientSounds(soundEntryPlayer, audio)
	{
#ifdef MMO_WITH_DEV_COMMANDS
		if (s_glueCameraPathVar == nullptr)
		{
			s_glueCameraPathVar = ConsoleVarMgr::RegisterConsoleVar("glueCameraPath", "Pins the glue scene camera to this point of the flight path (0 = gate, 1 = plaza). Negative to follow the director (developer builds only).", "-1");
		}
#endif

		m_scene = std::make_unique<OctreeScene>();
		m_scene->SetFogEnabled(true);

		m_sky = std::make_unique<SkyComponent>(*m_scene, nullptr);
		m_environment.SetTarget(m_environmentProfiles.Get(m_definition.environmentProfile), true);

		m_camera = m_scene->CreateCamera("Default");
		m_cameraNode = &m_scene->CreateSceneNode("GlueCamera");

		// Without a fixed yaw axis, LookAt turns the current orientation by the shortest arc every
		// frame, so the camera picks up roll that never goes away (a tilted horizon).
		m_cameraNode->SetFixedYawAxis(true, Vector3::UnitY);
		m_scene->GetRootSceneNode().AddChild(*m_cameraNode);
		m_cameraNode->AttachObject(*m_camera);

		m_worldRootNode = m_scene->GetRootSceneNode().CreateChildSceneNode();

		m_plazaAvatars = std::make_unique<PlazaAvatars>(*m_scene, m_project, m_definition);

		// Water pages resolve their surface material through this static hook. WorldState sets the
		// same resolver, but the glue scene is shown before any world was ever entered.
		terrain::Page::SetWaterMaterialResolver([&project = m_project](const terrain::WaterType waterType) -> String
			{
				const auto* profile = project.waterProfiles.getById(static_cast<uint32>(waterType));
				if (profile == nullptr || !profile->has_surface_material())
				{
					return String();
				}

				return profile->surface_material();
			});

		m_work = std::make_unique<asio::io_service::work>(m_workQueue);
		m_dispatcherWork = std::make_unique<asio::io_service::work>(m_dispatcher);
		auto& workQueue = m_workQueue;
		m_loaderThread = std::thread([&workQueue]()
			{
				workQueue.run();
			});

		m_pageQueue = m_definition.gatePages;
		m_pageQueue.insert(m_pageQueue.end(), m_definition.plazaPages.begin(), m_definition.plazaPages.end());
		m_gatePageCount = m_definition.gatePages.size();

		if (!LoadWorld())
		{
			// Nothing will ever load: an empty queue reports both views ready, so the screens are not
			// left behind a fade forever.
			m_pageQueue.clear();
			m_gatePageCount = 0;
		}

		UpdateCamera(0.0f);
	}

	GlueScene::~GlueScene()
	{
		// Characters and their item meshes go before anything else touches the scene.
		m_plazaAvatars.reset();

		m_work.reset();
		m_dispatcherWork.reset();
		m_workQueue.stop();
		m_dispatcher.stop();
		if (m_loaderThread.joinable())
		{
			m_loaderThread.join();
		}

		// The world instance never clears its ambient sound target itself.
		m_ambientSounds.Clear();
		m_worldInstance.reset();
		m_sky.reset();
		m_scene->Clear();
	}

	Scene& GlueScene::GetScene()
	{
		return *m_scene;
	}

	Vector3 GlueScene::GetCameraPosition() const
	{
		return m_camera->GetDerivedPosition();
	}

	void GlueScene::SetTimeOfDay(const GameTime msSinceMidnight)
	{
		const float normalized = static_cast<float>(static_cast<double>(msSinceMidnight % constants::OneDay) / static_cast<double>(constants::OneDay));
		m_sky->SetNormalizedTimeOfDay(normalized);
	}

	bool GlueScene::ConsumeFlightFinished()
	{
		const bool finished = m_flightFinished;
		m_flightFinished = false;
		return finished;
	}

	void GlueScene::SnapToPlazaWhenReady()
	{
		m_pendingArrival = PendingArrival::Snap;
	}

	void GlueScene::FlyInWhenReady()
	{
		m_pendingArrival = PendingArrival::FlyIn;
	}

	bool GlueScene::ConsumeDeferredFlightStarted()
	{
		const bool started = m_deferredFlightStarted;
		m_deferredFlightStarted = false;
		return started;
	}

	void GlueScene::SkipFlight()
	{
		if (m_pendingArrival == PendingArrival::FlyIn)
		{
			m_pendingArrival = PendingArrival::Snap;
		}

		m_director.Skip();
	}

	bool GlueScene::IsFlightActive() const
	{
		const GlueCameraMode mode = m_director.GetMode();
		return m_pendingArrival != PendingArrival::None || mode == GlueCameraMode::FlyingIn || mode == GlueCameraMode::FlyingOut;
	}

	void GlueScene::SnapToGate()
	{
		m_director.SnapToGate();

		// A flight that ended (or was skipped) since the last Update must not be reported after
		// the jump: drop both the director's flag and the latch.
		(void)m_director.ConsumeFlightFinished();
		m_flightFinished = false;

		m_pendingArrival = PendingArrival::None;
		m_deferredFlightStarted = false;
		m_pushInSeconds = 0.0f;
		m_pushInElapsed = 0.0f;
		m_pushInFinished = false;
	}

	void GlueScene::PushIn(const float seconds, const Vector3& characterPosition)
	{
		m_pushInCharacterPosition = characterPosition;

		if (seconds <= 0.0f)
		{
			// Finished at once: hold the end of the push-in.
			m_pushInSeconds = 1.0f;
			m_pushInElapsed = 1.0f;
			m_pushInFinished = true;
			return;
		}

		m_pushInSeconds = seconds;
		m_pushInElapsed = 0.0f;
		m_pushInFinished = false;
	}

	bool GlueScene::ConsumePushInFinished()
	{
		const bool finished = m_pushInFinished;
		m_pushInFinished = false;
		return finished;
	}

	bool GlueScene::ProjectToScreen(const Vector3& worldPosition, float& x, float& y) const
	{
		x = 0.0f;
		y = 0.0f;

		const Vector3 cameraToPosition = worldPosition - m_camera->GetDerivedPosition();
		if (cameraToPosition.Dot(m_camera->GetDerivedDirection()) <= 0.0f)
		{
			return false;
		}

		m_camera->GetNormalizedScreenPosition(worldPosition, x, y);
		return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
	}

	int32 GlueScene::PickPlazaCharacter(const float x, const float y) const
	{
		if (IsFlightActive() || x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
		{
			return -1;
		}

		return m_plazaAvatars->Pick(m_camera->GetCameraToViewportRay(x, y, pickDistance));
	}

	bool GlueScene::LoadWorld()
	{
		const String& assetPath = m_definition.world;

		m_worldInstance = std::make_shared<ClientWorldInstance>(*m_scene, *m_worldRootNode, assetPath, m_workQueue, m_dispatcher);
		m_worldInstance->SetAmbientSounds(&m_ambientSounds);

		const std::unique_ptr<std::istream> streamPtr = AssetRegistry::OpenFile(assetPath + ".hwld");
		if (!streamPtr)
		{
			ELOG("Glue scene: failed to open world file '" << assetPath << ".hwld'");
			return false;
		}

		io::StreamSource source{ *streamPtr };
		io::Reader reader{ source };

		ClientWorldInstanceDeserializer deserializer{ *m_worldInstance };
		if (!deserializer.Read(reader))
		{
			ELOG("Glue scene: failed to read world '" << assetPath << ".hwld'");
			return false;
		}

		return true;
	}

	void GlueScene::Update(const float deltaSeconds)
	{
		PROFILE_SCOPE("GlueScene::Update");

		UpdatePageLoading();

		{
			PROFILE_SCOPE("GlueScene::StreamingDispatch");

			const auto start = std::chrono::steady_clock::now();
			while (m_dispatcher.poll_one())
			{
				if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= dispatchBudgetMs)
				{
					break;
				}
			}
		}

		m_director.Update(deltaSeconds);
		if (m_director.ConsumeFlightFinished())
		{
			m_flightFinished = true;
		}

		// The plaza only becomes ready after the gate (or when both readiness timeouts fired), so
		// this also means the gate view the fly-in starts from is ready.
		if (m_pendingArrival != PendingArrival::None && m_plazaReady)
		{
			if (m_pendingArrival == PendingArrival::FlyIn)
			{
				// The list beat the scene on a fresh login: the fade held until now, and the flight
				// starts from the gate as it lifts.
				m_director.SnapToGate();
				m_director.FlyIn();
				m_deferredFlightStarted = true;
			}
			else
			{
				// Back from the world, or a skip arrived while waiting: cut straight to the plaza.
				m_director.SnapToPlaza();
				m_flightFinished = true;
			}

			m_pendingArrival = PendingArrival::None;
		}

		UpdateCamera(deltaSeconds);

		const Vector3 cameraPosition = m_camera->GetDerivedPosition();

		m_plazaAvatars->Update(deltaSeconds, cameraPosition);
		if (m_worldInstance && m_worldInstance->HasTerrain())
		{
			m_worldInstance->GetTerrain()->NotifyCameraPosition(cameraPosition);
		}

		m_ambientSounds.Update(cameraPosition, deltaSeconds);

		m_sky->Update(deltaSeconds, GetAsyncTimeMs());
		m_sky->SetPosition(cameraPosition);
		m_environment.Update(deltaSeconds, m_sky->GetNormalizedTimeOfDay());
		m_sky->ApplyEnvironment(m_environment.GetState());

		if (m_renderer)
		{
			if (m_exposureVar == nullptr)
			{
				// Looked up until it is registered, then kept: unregistering only flags a cvar, so
				// the pointer stays valid (IsValid tells whether it is registered right now).
				m_exposureVar = ConsoleVarMgr::FindConsoleVar("gxExposure");
			}

			const float brightness = (m_exposureVar && m_exposureVar->IsValid()) ? m_exposureVar->GetFloatValue() : 1.0f;
			ApplyEnvironmentStateToRenderer(*m_renderer, m_environment.GetState(), brightness);
		}
	}

	void GlueScene::UpdatePageLoading()
	{
		ApplyReadinessTimeout();

		// The gate becomes ready before any plaza page starts, so the plaza's entities cannot hold
		// back the gate view.
		if (!m_gateReady && m_nextPage >= m_gatePageCount)
		{
			if (!m_worldInstance || !m_worldInstance->HasPendingEntityLoads())
			{
				MarkGateReady();
			}

			return;
		}

		if (m_nextPage < m_pageQueue.size())
		{
			const auto [x, y] = m_pageQueue[m_nextPage++];
			LoadPage(x, y);
			return;
		}

		if (!m_plazaReady && m_gateReady)
		{
			if (!m_worldInstance || !m_worldInstance->HasPendingEntityLoads())
			{
				m_plazaReady = true;
				DLOG("Glue scene: plaza pages loaded after " << (GetAsyncTimeMs() - m_plazaLoadStartMs) << " ms");
			}
		}
	}

	void GlueScene::ApplyReadinessTimeout()
	{
		const GameTime now = GetAsyncTimeMs();

		if (!m_pageLoadingStarted)
		{
			// The pages load one per Update, so the gate's clock starts with the first one, not at
			// construction (the login screen may take a while to set up before its first frame).
			m_pageLoadingStarted = true;
			m_gateLoadStartMs = now;
		}

		if (!m_gateReady)
		{
			if (now - m_gateLoadStartMs >= readinessTimeoutMs)
			{
				WLOG("Glue scene: gate view not loaded after " << (now - m_gateLoadStartMs) << " ms, showing it anyway");
				MarkGateReady();
			}

			// The plaza's clock starts when the gate is ready.
			return;
		}

		if (!m_plazaReady && now - m_plazaLoadStartMs >= readinessTimeoutMs)
		{
			WLOG("Glue scene: plaza not loaded after " << (now - m_plazaLoadStartMs) << " ms, showing it anyway");
			m_plazaReady = true;
		}
	}

	void GlueScene::MarkGateReady()
	{
		m_gateReady = true;
		m_plazaLoadStartMs = GetAsyncTimeMs();
		DLOG("Glue scene: gate pages loaded after " << (m_plazaLoadStartMs - m_gateLoadStartMs) << " ms");
	}

	void GlueScene::LoadPage(const uint8 x, const uint8 y)
	{
		PROFILE_SCOPE("GlueScene::LoadPage");

		if (!m_worldInstance)
		{
			return;
		}

		if (m_worldInstance->HasTerrain())
		{
			if (terrain::Page* page = m_worldInstance->GetTerrain()->GetPage(x, y))
			{
				// Nothing else prepares pages of this terrain, so the preparation can always run
				// synchronously here (WorldState::WaitForWorldLoading without the in-flight case).
				if (!page->IsPrepared() && page->BeginPrepare())
				{
					if (page->PrepareParse())
					{
						page->FinalizePrepare();
					}
					else
					{
						page->AbortPrepare();
						WLOG("Glue scene: failed to prepare terrain page " << static_cast<int32>(x) << "," << static_cast<int32>(y));
					}
				}

				if (page->IsPrepared())
				{
					while (!page->Load())
					{
					}
				}
			}
		}

		m_worldInstance->LoadPageEntities(x, y);
	}

	void GlueScene::UpdateCamera(const float deltaSeconds)
	{
		float pathParameter = m_director.GetPathParameter();
		if (s_glueCameraPathVar && s_glueCameraPathVar->GetFloatValue() >= 0.0f)
		{
			pathParameter = s_glueCameraPathVar->GetFloatValue();
		}

		const GlueCameraKey key = EvaluateGluePath(m_definition.flightPath, pathParameter);
		Vector3 position = key.position;

		if (m_director.GetMode() == GlueCameraMode::Gate)
		{
			m_driftSeconds = std::fmod(m_driftSeconds + deltaSeconds, driftPeriodSeconds);

			// Sway sideways, across the view direction.
			Vector3 right = (key.lookAt - key.position).Cross(Vector3::UnitY);
			if (right.GetSquaredLength() > 0.0001f)
			{
				right.Normalize();
				position += right * (driftAmplitude * std::sin(m_driftSeconds / driftPeriodSeconds * 2.0f * Pi));
			}
		}

		if (m_pushInSeconds > 0.0f)
		{
			if (m_pushInElapsed < m_pushInSeconds)
			{
				m_pushInElapsed = std::min(m_pushInElapsed + deltaSeconds, m_pushInSeconds);
				if (m_pushInElapsed >= m_pushInSeconds)
				{
					m_pushInFinished = true;
				}
			}

			const float t = m_pushInElapsed / m_pushInSeconds;
			const float eased = t * t * (3.0f - 2.0f * t);
			const Vector3 target = m_pushInCharacterPosition + Vector3(0.0f, pushInTargetHeight, 0.0f);
			position += (target - position) * (pushInFraction * eased);
		}

		m_cameraNode->SetPosition(position);
		m_cameraNode->LookAt(key.lookAt, TransformSpace::World, Vector3::NegativeUnitZ);
	}
}

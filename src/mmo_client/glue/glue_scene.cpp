// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene.h"

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
#include "scene_graph/camera.h"
#include "scene_graph/octree_scene.h"
#include "scene_graph/scene_node.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

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
		m_scene->GetRootSceneNode().AddChild(*m_cameraNode);
		m_cameraNode->AttachObject(*m_camera);

		m_worldRootNode = m_scene->GetRootSceneNode().CreateChildSceneNode();

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
		m_work.reset();
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

		UpdateCamera(deltaSeconds);

		const Vector3 cameraPosition = m_camera->GetDerivedPosition();
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
			const ConsoleVar* exposureVar = ConsoleVarMgr::FindConsoleVar("gxExposure");
			const float brightness = exposureVar ? exposureVar->GetFloatValue() : 1.0f;
			ApplyEnvironmentStateToRenderer(*m_renderer, m_environment.GetState(), brightness);
		}
	}

	void GlueScene::UpdatePageLoading()
	{
		// The gate becomes ready before any plaza page starts, so the plaza's entities cannot hold
		// back the gate view.
		if (!m_gateReady && m_nextPage >= m_gatePageCount)
		{
			if (!m_worldInstance || !m_worldInstance->HasPendingEntityLoads())
			{
				m_gateReady = true;
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
			}
		}
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

		m_cameraNode->SetPosition(position);
		m_cameraNode->LookAt(key.lookAt, TransformSpace::World, Vector3::NegativeUnitZ);
	}
}

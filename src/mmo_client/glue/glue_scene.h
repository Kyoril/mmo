// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "game_client/ambient_sound_emitters.h"
#include "game_client/glue_camera_director.h"
#include "game_client/glue_scene_definition.h"
#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_profile_proto.h"

#include <asio/io_service.hpp>

#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace mmo
{
	class Camera;
	class ClientWorldInstance;
	class DeferredRenderer;
	class IAudio;
	class Scene;
	class SceneNode;
	class SkyComponent;
	class SoundEntryPlayer;

	/// The 3D scene behind the glue screens: a few pages of Haven around its gate and plaza, lit
	/// by the shared sky and environment, with a camera driven along the scene definition's path.
	/// @remark Main thread only, except for the loader thread, which only reads page entity files
	///         posted by the world instance (the same work WorldState's background loader does).
	class GlueScene final : public NonCopyable
	{
	public:
		/// Creates the scene and starts loading the gate pages.
		/// @param project The client project, for environment and water profiles. Must outlive this.
		/// @param definition The scene definition.
		/// @param soundEntryPlayer Plays the ambient sounds of placed meshes.
		/// @param audio The audio system.
		GlueScene(const proto_client::Project& project, GlueSceneDefinition definition, SoundEntryPlayer& soundEntryPlayer, IAudio& audio);

		/// Joins the loader thread and tears the world down before the scene.
		~GlueScene();

	public:
		/// Loads the next page, pumps the dispatcher with a 4 ms budget, advances the director
		/// and the camera, then the sky and the environment.
		/// @param deltaSeconds Real time since the last update.
		void Update(float deltaSeconds);

		/// Whether the gate pages and every entity on them are loaded.
		[[nodiscard]] bool IsGateReady() const { return m_gateReady; }

		/// Whether the plaza pages (after the gate pages) and every entity on them are loaded.
		[[nodiscard]] bool IsPlazaReady() const { return m_plazaReady; }

		/// Gets the scene, for the frame renderer.
		[[nodiscard]] Scene& GetScene();

		/// Gets the camera director.
		[[nodiscard]] GlueCameraDirector& GetDirector() { return m_director; }

		/// Gets the scene definition.
		[[nodiscard]] const GlueSceneDefinition& GetDefinition() const { return m_definition; }

		/// Gets the camera's current world position, for the audio listener.
		[[nodiscard]] Vector3 GetCameraPosition() const;

		/// Sets the time of day.
		/// @param msSinceMidnight Milliseconds since midnight.
		void SetTimeOfDay(GameTime msSinceMidnight);

		/// Sets the renderer the environment is applied to, or null for none.
		void SetRenderer(DeferredRenderer* renderer) { m_renderer = renderer; }

		/// Whether a flight ended since the last call. Latched from the director every Update.
		[[nodiscard]] bool ConsumeFlightFinished();

	private:
		/// Loads the world file and creates the world instance.
		bool LoadWorld();

		/// Loads the next queued page, if any, and updates the ready flags.
		void UpdatePageLoading();

		/// Synchronously prepares and loads one terrain page, then starts loading its entities.
		void LoadPage(uint8 x, uint8 y);

		/// Places the camera on the path, with the idle drift at the gate.
		void UpdateCamera(float deltaSeconds);

	private:
		const proto_client::Project& m_project;
		GlueSceneDefinition m_definition;
		GlueCameraDirector m_director;

		std::unique_ptr<Scene> m_scene;
		SceneNode* m_worldRootNode = nullptr;
		SceneNode* m_cameraNode = nullptr;
		Camera* m_camera = nullptr;
		std::unique_ptr<SkyComponent> m_sky;

		EnvironmentController m_environment;
		EnvironmentProfileCache<proto_client::EnvironmentProfileManager> m_environmentProfiles;

		AmbientSoundEmitters m_ambientSounds;

		DeferredRenderer* m_renderer = nullptr;

		/// Gate pages first, then plaza pages.
		std::vector<std::pair<uint8, uint8>> m_pageQueue;
		size_t m_nextPage = 0;
		size_t m_gatePageCount = 0;
		bool m_gateReady = false;
		bool m_plazaReady = false;

		bool m_flightFinished = false;
		float m_driftSeconds = 0.0f;

		// Declared after the scene: destroyed first, so handlers still queued in them (which may
		// hold the world instance) are released while the scene is alive.
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::unique_ptr<asio::io_service::work> m_work;
		std::thread m_loaderThread;

		std::shared_ptr<ClientWorldInstance> m_worldInstance;
	};
}

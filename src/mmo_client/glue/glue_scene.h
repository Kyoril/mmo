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
	class ConsoleVar;
	class DeferredRenderer;
	class IAudio;
	class PlazaAvatars;
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
		/// Loads the next page, pumps the dispatcher with a 4 ms budget, advances the director,
		/// the camera and the plaza characters, then the sky and the environment.
		/// @param deltaSeconds Real time since the last update.
		void Update(float deltaSeconds);

		/// Whether the gate pages and every entity on them are loaded, or loading them took longer
		/// than the readiness timeout (8 s), so no fade waits for the gate forever.
		[[nodiscard]] bool IsGateReady() const { return m_gateReady; }

		/// Whether the plaza pages (after the gate pages) and every entity on them are loaded, or
		/// loading them took longer than the readiness timeout (8 s after the gate became ready),
		/// so neither a held fade nor the flight input lock lasts forever.
		[[nodiscard]] bool IsPlazaReady() const { return m_plazaReady; }

		/// Gets the scene, for the frame renderer.
		[[nodiscard]] Scene& GetScene();

		/// Gets the characters standing on the plaza.
		[[nodiscard]] PlazaAvatars& GetPlazaAvatars() { return *m_plazaAvatars; }

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

		/// Whether a flight ended since the last call. Latched from the director every Update, and
		/// also set when a pending FadeToPlazaWhenReady snapped to the plaza.
		[[nodiscard]] bool ConsumeFlightFinished();

		/// Snaps the camera to the plaza as soon as the plaza is ready. Until then IsFlightActive
		/// is true; the snap counts as a finished flight (see ConsumeFlightFinished).
		void FadeToPlazaWhenReady();

		/// Whether the camera is flying, or waiting for the plaza to snap to it.
		[[nodiscard]] bool IsFlightActive() const;

		/// Jumps to the gate, dropping a pending plaza snap and the push-in.
		void SnapToGate();

		/// Eases the camera part of the way towards the front character, then holds it there.
		/// @param seconds Duration of the push-in. 0 or less finishes it at once.
		void PushIn(float seconds);

		/// Whether a push-in finished since the last call.
		[[nodiscard]] bool ConsumePushInFinished();

		/// Projects a world position onto the screen.
		/// @param worldPosition The position to project.
		/// @param x Receives the horizontal position, 0 (left) to 1 (right).
		/// @param y Receives the vertical position, 0 (top) to 1 (bottom).
		/// @return False when the position is behind the camera or off screen.
		[[nodiscard]] bool ProjectToScreen(const Vector3& worldPosition, float& x, float& y) const;

	private:
		/// Loads the world file and creates the world instance.
		bool LoadWorld();

		/// Loads the next queued page, if any, and updates the ready flags.
		void UpdatePageLoading();

		/// Starts the gate's readiness clock on the first call, then marks the gate, and after it
		/// the plaza, ready when loading it took longer than the timeout.
		void ApplyReadinessTimeout();

		/// Marks the gate ready and starts the plaza's readiness clock.
		void MarkGateReady();

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

		/// The characters on the plaza. Reset explicitly before the scene is cleared.
		std::unique_ptr<PlazaAvatars> m_plazaAvatars;

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

		/// Whether the first Update started loading pages (and the gate's readiness clock).
		bool m_pageLoadingStarted = false;

		/// When loading the gate pages began (the first Update), in GetAsyncTimeMs milliseconds.
		GameTime m_gateLoadStartMs = 0;

		/// When loading the plaza pages began (the gate became ready), in GetAsyncTimeMs milliseconds.
		GameTime m_plazaLoadStartMs = 0;

		/// The gxExposure cvar, once it is registered (the renderer registers it).
		const ConsoleVar* m_exposureVar = nullptr;

		bool m_flightFinished = false;
		float m_driftSeconds = 0.0f;

		/// Set by FadeToPlazaWhenReady until the plaza is ready.
		bool m_plazaSnapPending = false;

		/// Duration of the push-in, or 0 while there is none.
		float m_pushInSeconds = 0.0f;
		float m_pushInElapsed = 0.0f;
		bool m_pushInFinished = false;

		// Declared after the scene: destroyed first, so handlers still queued in them (which may
		// hold the world instance) are released while the scene is alive.
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::unique_ptr<asio::io_service::work> m_work;

		/// Keeps m_dispatcher from stopping when a poll finds it empty. Without it asio stops the
		/// dispatcher after the first empty poll, and every later poll runs at most one handler
		/// (Windows) or none at all (macOS), so the world instance's preparation chains stall.
		std::unique_ptr<asio::io_service::work> m_dispatcherWork;
		std::thread m_loaderThread;

		std::shared_ptr<ClientWorldInstance> m_worldInstance;
	};
}

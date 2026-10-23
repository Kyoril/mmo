// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "game_client/ambient_sound_emitters.h"
#include "game_client/glue_camera_director.h"
#include "game_common/glue_bake.h"
#include "game_common/glue_scene_definition.h"
#include "scene_graph/environment_controller.h"
#include "scene_graph/environment_profile_proto.h"

#include <asio/io_service.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace mmo
{
	class Camera;
	class ClientWorldInstance;
	class ConsoleVar;
	class DeferredRenderer;
	class Entity;
	class IAudio;
	class InstancedFoliage;
	class PlazaAvatars;
	class Scene;
	class SceneNode;
	class SkyComponent;
	class SoundEntryPlayer;
	class WorldModelBatch;

	/// The 3D scene behind the glue screens: a few pages of Haven around its gate and plaza, lit
	/// by the shared sky and environment, with a camera driven along the scene definition's path.
	/// The scene comes from the glue bake (Interface/GlueUI/GlueBake/GlueBake.json, written by
	/// mmo_edit --bake-glue-scene) when it was baked from the current scene definition; otherwise
	/// the world's pages are streamed as in the game.
	/// @remark Main thread only, except for the loader thread of the streaming fallback, which only
	///         reads page entity files posted by the world instance (the same work WorldState's
	///         background loader does), and the bake's prefetch thread, which only calls
	///         AssetRegistry::Prefetch.
	class GlueScene final : public NonCopyable
	{
	public:
		/// Creates the scene and reads the glue bake, or starts streaming the gate pages when the
		/// bake is missing, unparsable or was baked from another scene definition.
		/// @param project The client project, for environment and water profiles. Must outlive this.
		/// @param definition The scene definition.
		/// @param definitionHash HashGlueBakeBytes of the scene definition file, matched against the
		///        bake's sceneHash.
		/// @param soundEntryPlayer Plays the ambient sounds of placed meshes.
		/// @param audio The audio system.
		GlueScene(const proto_client::Project& project, GlueSceneDefinition definition, const String& definitionHash, SoundEntryPlayer& soundEntryPlayer, IAudio& audio);

		/// Joins the loader thread and tears the world down before the scene.
		~GlueScene();

	public:
		/// Creates the next part of the bake (or loads the next page and pumps the dispatcher with a
		/// 4 ms budget), advances the director, the camera and the plaza characters, then the sky
		/// and the environment.
		/// @param deltaSeconds Real time since the last update.
		void Update(float deltaSeconds);

		/// Whether every entity of the bake exists, or, when streaming, every page's terrain is
		/// loaded and every world model the gate camera sees was created (see
		/// HasPendingGateContent), or loading took longer than the readiness timeout (8 s), so no
		/// fade waits for the gate forever.
		[[nodiscard]] bool IsGateReady() const { return m_gateReady; }

		/// Whether, after the gate, every entity of every page is loaded (with the bake: together
		/// with the gate, as the bake holds both views), or
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

		/// Updates the audio listener to match the glue camera's current position and facing.
		void UpdateAudioListener(IAudio& audio, float deltaSeconds);

		/// Sets the time of day.
		/// @param msSinceMidnight Milliseconds since midnight.
		void SetTimeOfDay(GameTime msSinceMidnight);

		/// Sets the renderer the environment is applied to, or null for none.
		void SetRenderer(DeferredRenderer* renderer) { m_renderer = renderer; }

		/// Whether a flight ended since the last call. Latched from the director every Update, and
		/// also set when a pending SnapToPlazaWhenReady snapped to the plaza.
		[[nodiscard]] bool ConsumeFlightFinished();

		/// Snaps the camera to the plaza as soon as the plaza is ready. Until then IsFlightActive
		/// is true; the snap counts as a finished flight (see ConsumeFlightFinished).
		void SnapToPlazaWhenReady();

		/// Starts the fly-in from the gate as soon as gate and plaza are ready (or their readiness
		/// timeouts fired). Until then IsFlightActive is true; when the flight starts,
		/// ConsumeDeferredFlightStarted reports it once so the fade can lift on the gate view.
		void FlyInWhenReady();

		/// Whether a deferred fly-in (FlyInWhenReady) started since the last call.
		[[nodiscard]] bool ConsumeDeferredFlightStarted();

		/// Skips a running flight. A fly-in still waiting for the scene becomes a plaza snap when
		/// the scene is ready, so a skip requested early (enterworld) still lands on the plaza.
		void SkipFlight();

		/// Whether the camera is flying, or waiting for the scene to fly or snap to the plaza.
		[[nodiscard]] bool IsFlightActive() const;

		/// Jumps to the gate, dropping a pending plaza arrival and the push-in.
		void SnapToGate();

		/// Eases the camera part of the way towards a character, then holds it there.
		/// @param seconds Duration of the push-in. 0 or less finishes it at once.
		/// @param characterPosition Where the character stands (its feet); the camera moves
		///        towards a point about chest high above it.
		void PushIn(float seconds, const Vector3& characterPosition);

		/// Whether a push-in finished since the last call.
		[[nodiscard]] bool ConsumePushInFinished();

		/// Projects a world position onto the screen.
		/// @param worldPosition The position to project.
		/// @param x Receives the horizontal position, 0 (left) to 1 (right).
		/// @param y Receives the vertical position, 0 (top) to 1 (bottom).
		/// @return False when the position is behind the camera or off screen.
		[[nodiscard]] bool ProjectToScreen(const Vector3& worldPosition, float& x, float& y) const;

		/// Finds the plaza character under a screen position, with a ray from the camera.
		/// @param x Horizontal position, 0 (left) to 1 (right).
		/// @param y Vertical position, 0 (top) to 1 (bottom).
		/// @return The character's order index, or -1 for none (or while the camera flies).
		[[nodiscard]] int32 PickPlazaCharacter(float x, float y) const;

	private:
		/// Reads the glue bake manifest and decides whether the scene comes from it. Logs a warning
		/// with the reason when it does not.
		/// @param definitionHash HashGlueBakeBytes of the current scene definition file.
		/// @return The manifest to build the scene from, or nullopt to stream the world.
		[[nodiscard]] static std::optional<GlueBakeManifest> ReadBake(const String& definitionHash);

		/// Starts a thread that prefetches every file the bake's steps will load.
		/// @param manifest The manifest to build the scene from.
		void StartBakePrefetch(const GlueBakeManifest& manifest);

		/// Queues the steps that create the bake's entities, batches, lights, sounds and particles.
		/// @param manifest The manifest to build the scene from.
		void QueueBakeSteps(std::shared_ptr<const GlueBakeManifest> manifest);

		/// Runs queued bake steps for up to a frame budget; marks gate and plaza ready after the last.
		void UpdateBakeLoading();

		/// Creates instanced batches (or, for meshes that cannot be instanced, entities) for one
		/// group of baked mesh placements.
		/// @param meshes The placements the group indexes.
		/// @param group The group to create.
		void CreateBakeMeshGroup(const std::vector<GlueBakeMesh>& meshes, const GlueBakeMeshGroup& group);

		/// Creates an entity for a baked mesh under the world root.
		/// @param mesh The mesh to place.
		/// @param transform Its world transform.
		/// @param materialOverrides Material per submesh; empty entries keep the mesh's own.
		/// @return The entity, or null when the mesh cannot be loaded.
		Entity* CreateBakeEntity(const String& mesh, const Matrix4& transform, const std::vector<String>& materialOverrides);

		/// Creates the entity of a baked terrain tile, with its splatting (near) or albedo (far)
		/// texture set to the sampler state the bake requires.
		void CreateBakeTerrainTile(const GlueBakeTerrainTile& tile);

		/// Creates the bake's foliage as hardware-instanced cells.
		void CreateBakeFoliage(const std::vector<GlueBakeFoliage>& foliage);

		/// Creates a scene light for a baked light.
		void CreateBakeLight(const GlueBakeLight& light);

		/// Creates a playing particle emitter for a baked emitter placement.
		void CreateBakeParticles(const GlueBakeParticles& particles);

		/// Loads the world file and creates the world instance.
		bool LoadWorld();

		/// Loads the next queued page, if any, and updates the ready flags.
		void UpdatePageLoading();

		/// Starts the gate's readiness clock on the first call, then marks the gate, and after it
		/// the plaza, ready when loading it took longer than the timeout.
		void ApplyReadinessTimeout();

		/// Marks the gate ready, logs how long its terrain and its world models took, and starts the
		/// plaza's readiness clock.
		/// @param timedOut Whether the readiness timeout forced it, which is logged instead of the split.
		void MarkGateReady(bool timedOut);

		/// Whether content the gate camera sees is still loading: an entity or foliage file not
		/// handled yet, or a world model in the gate camera's frustum still being prepared. Once the
		/// camera left the gate, anything still loading counts. Requires the world instance.
		[[nodiscard]] bool HasPendingGateContent() const;

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
		bool m_gateReady = false;
		bool m_plazaReady = false;

		/// Whether the first Update started loading pages (and the gate's readiness clock).
		bool m_pageLoadingStarted = false;

		/// When loading the gate pages began (the first Update), in GetAsyncTimeMs milliseconds.
		GameTime m_gateLoadStartMs = 0;

		/// When loading the plaza pages began (the gate became ready), in GetAsyncTimeMs milliseconds.
		GameTime m_plazaLoadStartMs = 0;

		/// When the last page's terrain was loaded, in GetAsyncTimeMs milliseconds; 0 until then.
		GameTime m_terrainLoadedMs = 0;

		/// The gxExposure cvar, once it is registered (the renderer registers it).
		const ConsoleVar* m_exposureVar = nullptr;

		bool m_flightFinished = false;
		float m_driftSeconds = 0.0f;

		/// A plaza arrival waiting for the scene to be ready.
		enum class PendingArrival
		{
			None,
			/// SnapToPlazaWhenReady: cut to the plaza.
			Snap,
			/// FlyInWhenReady: fly in from the gate.
			FlyIn
		};

		/// Set by SnapToPlazaWhenReady or FlyInWhenReady until the plaza is ready.
		PendingArrival m_pendingArrival = PendingArrival::None;

		/// Latched when a deferred fly-in starts, until ConsumeDeferredFlightStarted.
		bool m_deferredFlightStarted = false;

		/// Duration of the push-in, or 0 while there is none.
		float m_pushInSeconds = 0.0f;
		float m_pushInElapsed = 0.0f;
		bool m_pushInFinished = false;

		/// Where the character the push-in moves towards stands.
		Vector3 m_pushInCharacterPosition;

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

		/// Whether the scene comes from the glue bake (no world instance, no loader thread).
		bool m_useBake = false;

		/// What a bake step creates, for the timing split of the bake-loaded log line.
		enum BakePhase : uint8
		{
			BakePhaseTerrain,
			BakePhaseWater,
			BakePhaseMeshes,
			BakePhaseDecoration,
			BakePhaseCount
		};

		/// One step of creating the bake.
		struct BakeStep
		{
			BakePhase phase = BakePhaseTerrain;
			std::function<void()> run;
		};

		/// The bake's creation steps, run in order over the first frames.
		std::vector<BakeStep> m_bakeSteps;
		size_t m_nextBakeStep = 0;

		/// Milliseconds spent reading and parsing the manifest.
		double m_bakeReadMs = 0.0;

		/// Milliseconds spent per phase, and the number of frames the steps took.
		double m_bakePhaseMs[BakePhaseCount] = {};
		uint32 m_bakeFrames = 0;

		/// An instanced batch of baked placements and the node it is attached to.
		struct BakeBatch
		{
			std::shared_ptr<WorldModelBatch> batch;
			SceneNode* node = nullptr;
		};

		std::vector<BakeBatch> m_bakeBatches;

		/// The bake's trees and plants.
		std::unique_ptr<InstancedFoliage> m_bakeFoliage;

		/// Reads the bake's files (and what they reference) into memory while the glue UI loads,
		/// so the main thread's mesh loads skip the file system. Only calls AssetRegistry::Prefetch.
		std::thread m_bakePrefetchThread;

		/// Asks the prefetch thread to stop early (the scene is going away).
		std::atomic<bool> m_stopBakePrefetch{ false };

		/// Counters for the bake-loaded log line.
		uint32 m_bakeEntityCount = 0;
		uint32 m_bakeInstanceCount = 0;
		uint64 m_bakeTriangleCount = 0;

		/// Makes the names of the bake's scene objects unique.
		uint32 m_bakeObjectCounter = 0;
	};
}

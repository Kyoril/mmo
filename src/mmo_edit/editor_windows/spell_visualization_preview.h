// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "audio/audio.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/entity.h"
#include "scene_graph/animation_notify.h"

#include "game_common/projectile_target.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <vector>
#include <memory>

// Windows.h (pulled in via deferred_renderer.h -> d3d11.h) defines PlaySound as a macro
#ifdef PlaySound
#undef PlaySound
#endif

namespace mmo
{
	namespace proto
	{
		class SpellVisualization;
		class SpellKit;
	}

	class EditorHost;
	class Camera;
	class SceneNode;
	class Light;
	class EditorProjectileManager;

	/// @brief Spell visual event that can be triggered for preview.
	enum class PreviewEvent
	{
		StartCast,
		Casting,
		CastSucceeded,
		Impact,
		CancelCast
	};

	class IAudio;

	/// @brief Embedded 3D preview panel for spell visualizations.
	/// 
	/// This class provides a real-time 3D preview of spell effects directly
	/// within the spell visualization editor. It renders a scene with caster
	/// and target models, and can visualize any spell event with its associated
	/// kits (animations, sounds, particles, tints).
	class SpellVisualizationPreview
	{
	public:
		/// @brief Constructor.
		/// @param host The editor host for signals.
		/// @param audioSystem Optional audio system for sound playback.
		explicit SpellVisualizationPreview(EditorHost& host, IAudio* audioSystem = nullptr);

		/// @brief Destructor.
		~SpellVisualizationPreview();

		/// @brief Renders the 3D scene (called before UI drawing).
		void Update();

		/// @brief Draws the 3D viewport ImGui panel.
		/// @param visualization The current visualization being edited (can be null).
		/// @param panelId Unique ID for the ImGui panel.
		void DrawViewport(proto::SpellVisualization* visualization, const String& panelId);

		/// @brief Draws the preview control toolbar.
		/// @param visualization The current visualization being edited.
		void DrawToolbar(proto::SpellVisualization* visualization);

		/// @brief Triggers a specific spell event for preview.
		/// @param event The event to trigger.
		void TriggerEvent(PreviewEvent event);

		/// @brief Triggers the full spell cast sequence (StartCast -> Casting -> CastSucceeded -> Impact).
		void TriggerFullCastSequence();

		/// @brief Stops all active previews and resets to idle.
		void StopPreview();

		/// @brief Sets the caster model.
		/// @param meshPath Path to the mesh file.
		void SetCasterMesh(const String& meshPath);

		/// @brief Sets the target model.
		/// @param meshPath Path to the mesh file.
		void SetTargetMesh(const String& meshPath);

		/// @brief Updates the preview to reflect changes in the visualization data.
		/// @param visualization The visualization to sync with.
		void SyncWithVisualization(const proto::SpellVisualization& visualization);

		/// @brief Sets the projectile preview speed.
		/// @param speed Speed in units per second.
		void SetProjectileSpeed(float speed);

	private:
		/// @brief Creates the floor plane mesh.
		void CreateFloorPlane();

		/// @brief Creates the caster and target entities.
		void CreateCharacterEntities();

		/// @brief Updates active animation states.
		/// @param deltaTime Time since last update.
		void UpdateAnimations(float deltaTime);

		/// @brief Starts the projectile flight using EditorProjectileManager.
		void StartProjectile();

		/// @brief Cleans up all spell effect objects.
		void CleanupSpellEffects();

		/// @brief Applies a kit's effects (animation, sounds, tint).
		/// @param visualization The source visualization.
		/// @param eventValue The event enum value.
		void ApplyEventKits(proto::SpellVisualization* visualization, uint32 eventValue);

		/// @brief Plays a sound file with fade-in.
		/// @param soundPath Path to the sound file.
		void PlaySound(const String& soundPath);

		/// @brief Fades out and stops all currently playing sounds.
		void StopAllSounds();

		/// @brief Fades out sounds from the previous event (allows overlap during transition).
		void FadeOutPreviousSounds();

		/// @brief Updates all fading sound channels.
		/// @param deltaTime Time since last update.
		void UpdateSoundFades(float deltaTime);

		/// @brief Called when a projectile impacts its target.
		/// @param target The target that was hit.
		void OnProjectileImpact(IProjectileTarget* target);

		/// @brief Called when an animation notify is triggered.
		/// @param notify The notification that was triggered.
		/// @param animName The name of the animation.
		/// @param state The animation state.
		void OnAnimationNotify(const AnimationNotify& notify, const String& animName, const AnimationState& state);

		/// @brief Connects to animation notify signals for an entity.
		/// @param entity The entity to connect to.
		void ConnectAnimationNotifySignals(Entity* entity);

		/// @brief Gets the world position of a bone on an entity, or fallback offset.
		/// @param entity The entity with skeleton.
		/// @param entityNode The scene node of the entity.
		/// @param boneName Name of the bone to find.
		/// @param fallbackOffset Fallback offset from entity position if bone not found.
		/// @return World-space position.
		Vector3 GetBoneWorldPosition(Entity* entity, SceneNode* entityNode, const String& boneName, const Vector3& fallbackOffset);

		/// @brief Spawns particle emitters for a kit, optionally attached to a bone.
		/// @param kit The spell kit containing particle definitions.
		/// @param entity The entity to attach to (for bone attachment).
		/// @param entityNode Scene node of the entity (for positional fallback).
		void SpawnKitParticles(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode);

		/// @brief Creates a point light for a kit, optionally at a bone.
		/// @param kit The spell kit containing light definition.
		/// @param entity The entity to attach to (for bone attachment).
		/// @param entityNode Scene node of the entity (for positional fallback).
		void SpawnKitLight(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode, bool instantEvent = false);

		/// @brief Creates a ribbon trail for a kit, optionally at a bone.
		/// @param kit The spell kit containing ribbon trail definition.
		/// @param entity The entity to attach to (for bone attachment).
		/// @param entityNode Scene node of the entity (for positional fallback).
		void SpawnKitRibbonTrail(const proto::SpellKit& kit, Entity* entity, SceneNode* entityNode);

		/// @brief Spawns an impact particle emitter at a given position.
		/// @param particleName Asset path of the particle system.
		/// @param position World position to spawn at.
		void SpawnImpactParticle(const String& particleName, const Vector3& position);

	private:
		EditorHost& m_host;
		IAudio* m_audioSystem{ nullptr };
		scoped_connection m_updateConnection;
		scoped_connection m_projectileImpactConnection;
		scoped_connection_container m_animNotifyConnections;

		// Viewport state
		ImVec2 m_viewportSize{ 0, 0 };
		std::unique_ptr<DeferredRenderer> m_deferredRenderer;
		bool m_wireFrame{ false };

		// Scene objects
		Scene m_scene;
		SceneNode* m_cameraAnchor{ nullptr };
		SceneNode* m_cameraNode{ nullptr };
		Camera* m_camera{ nullptr };
		Light* m_sunLight{ nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;

		// Floor plane
		Entity* m_floorEntity{ nullptr };
		SceneNode* m_floorNode{ nullptr };

		// Character entities
		Entity* m_casterEntity{ nullptr };
		SceneNode* m_casterNode{ nullptr };
		AnimationState* m_casterAnimState{ nullptr };
		String m_casterMeshPath{ "Models/Character/Human/Female/HumanFemale_V2.hmsh" };

		Entity* m_targetEntity{ nullptr };
		SceneNode* m_targetNode{ nullptr };
		AnimationState* m_targetAnimState{ nullptr };
		String m_targetMeshPath{ "Models/Creatures/Boar/Boar.hmsh" };

		// Spell effect objects (legacy)
		ParticleEmitter* m_castParticles{ nullptr };
		ParticleEmitter* m_impactParticles{ nullptr };

		// Kit-spawned effect objects (new)
		/// @brief Particle emitters spawned by kits, cleaned up on event change/stop.
		std::vector<ParticleEmitter*> m_kitParticles;
		/// @brief Scene nodes created for kit effects.
		std::vector<SceneNode*> m_kitEffectNodes;
		/// @brief Ribbon trails spawned by kits.
		std::vector<RibbonTrail*> m_kitRibbonTrails;

		/// @brief Tracks a fading light with current and target intensity.
		struct FadingLight
		{
			Light* light{ nullptr };
			float currentIntensity{ 0.0f };
			float targetIntensity{ 0.0f };
			float fadeInSpeed{ 0.0f };
			float fadeOutSpeed{ 0.0f };
			bool fadingOut{ false };
			bool autoFadeOut{ false };
		};

		/// @brief Active fading lights (includes kit-spawned and pending fade-out).
		std::vector<FadingLight> m_fadingLights;

		/// @brief Counter for unique naming of spawned effects.
		uint32 m_effectCounter{ 0 };

		// Current visualization being previewed
		proto::SpellVisualization* m_currentVisualization{ nullptr };

		// Preview projectile speed
		float m_projectileSpeed{ 15.0f };

		// Projectile manager (uses editor-specific implementation)
		std::unique_ptr<EditorProjectileManager> m_projectileManager;
		std::shared_ptr<StaticProjectileTarget> m_projectileTarget;

		// Cast sequence state
		bool m_castSequenceActive{ false };
		PreviewEvent m_currentSequenceEvent{ PreviewEvent::StartCast };
		float m_sequenceTimer{ 0.0f };
		float m_castDuration{ 1.5f };
		bool m_loopSequence{ false };
		bool m_projectileSpawned{ false };
		bool m_waitingForSpellGo{ false };
		bool m_hasCastSucceededAnimation{ false };

		// Sound channel with fade state
		struct FadingChannel
		{
			ChannelIndex channel{ InvalidChannel };
			float currentVolume{ 0.0f };
			float targetVolume{ 1.0f };
			float fadeSpeed{ 2.0f }; // Volume units per second
			bool markedForRemoval{ false };
		};

		// Active sound channels with fade state
		std::vector<FadingChannel> m_fadingChannels;

		// Mouse interaction
		int16 m_lastMouseX{ 0 };
		int16 m_lastMouseY{ 0 };
		bool m_leftButtonPressed{ false };
		bool m_rightButtonPressed{ false };
		bool m_middleButtonPressed{ false };

		// Animation timing
		std::chrono::high_resolution_clock::time_point m_lastUpdateTime;
	};
}

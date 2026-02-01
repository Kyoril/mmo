// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "audio/audio.h"
#include "base/signal.h"
#include "base/typedefs.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/entity.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class SpellVisualization;
	}

	class EditorHost;
	class Camera;
	class SceneNode;
	class Light;

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

	private:
		/// @brief Creates the floor plane mesh.
		void CreateFloorPlane();

		/// @brief Creates the caster and target entities.
		void CreateCharacterEntities();

		/// @brief Updates active projectile simulation.
		/// @param deltaTime Time since last update.
		void UpdateProjectile(float deltaTime);

		/// @brief Updates active animation states.
		/// @param deltaTime Time since last update.
		void UpdateAnimations(float deltaTime);

		/// @brief Starts the projectile flight.
		void StartProjectile();

		/// @brief Cleans up all spell effect objects.
		void CleanupSpellEffects();

		/// @brief Applies a kit's effects (animation, sounds, tint).
		/// @param visualization The source visualization.
		/// @param eventValue The event enum value.
		void ApplyEventKits(proto::SpellVisualization* visualization, uint32 eventValue);

		/// @brief Plays a sound file.
		/// @param soundPath Path to the sound file.
		void PlaySound(const String& soundPath);

		/// @brief Stops all currently playing sounds.
		void StopAllSounds();

	private:
		EditorHost& m_host;
		IAudio* m_audioSystem{ nullptr };
		scoped_connection m_updateConnection;

		// Viewport state
		ImVec2 m_viewportSize{ 0, 0 };
		RenderTexturePtr m_viewportRT;
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

		// Spell effect objects
		ParticleEmitter* m_castParticles{ nullptr };
		ParticleEmitter* m_impactParticles{ nullptr };
		Entity* m_projectileEntity{ nullptr };
		SceneNode* m_projectileNode{ nullptr };

		// Current visualization being previewed
		proto::SpellVisualization* m_currentVisualization{ nullptr };

		// Projectile state
		bool m_projectileActive{ false };
		Vector3 m_projectileStartPos;
		Vector3 m_projectileTargetPos;
		float m_projectileProgress{ 0.0f };
		float m_projectileSpeed{ 15.0f };

		// Cast sequence state
		bool m_castSequenceActive{ false };
		PreviewEvent m_currentSequenceEvent{ PreviewEvent::StartCast };
		float m_sequenceTimer{ 0.0f };
		float m_castDuration{ 1.5f };
		bool m_loopSequence{ false };

		// Active sound channels for cleanup
		std::vector<ChannelIndex> m_activeChannels;

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

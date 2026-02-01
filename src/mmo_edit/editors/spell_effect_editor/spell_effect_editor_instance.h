// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>

#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/ribbon_trail.h"
#include "scene_graph/entity.h"

namespace mmo
{
	class SpellEffectEditor;
	class PreviewProviderManager;
	class Camera;
	class SceneNode;
	class Light;

	/// @brief Editor instance for previewing spell effects in 3D.
	/// 
	/// This instance creates a 3D scene with:
	/// - A floor plane with default material
	/// - A caster model (human female)
	/// - A target model (boar)
	/// - Support for spell visualization preview (particles, ribbons, projectiles)
	class SpellEffectEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Constructor.
		/// @param host The editor host.
		/// @param editor The parent editor.
		/// @param previewManager Preview provider manager.
		/// @param asset Path to the preview file.
		explicit SpellEffectEditorInstance(
			EditorHost& host, 
			SpellEffectEditor& editor, 
			PreviewProviderManager& previewManager,
			Path asset);
		
		/// @brief Destructor.
		~SpellEffectEditorInstance() override;

	public:
		/// @brief Renders the actual 3D viewport content.
		void Render();

		/// @brief Draws the editor UI.
		void Draw() override;
		
		/// @brief Handles mouse button down events.
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		/// @brief Handles mouse button up events.
		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		/// @brief Handles mouse move events.
		void OnMouseMoved(uint16 x, uint16 y) override;

		/// @brief Saves the preview configuration.
		bool Save() override;

	private:
		/// @brief Draws the 3D viewport panel.
		void DrawViewport(const String& id);
		
		/// @brief Draws the spell effect controls panel.
		void DrawControls(const String& id);
		
		/// @brief Draws the spell selection panel.
		void DrawSpellSelection(const String& id);
		
		/// @brief Draws the projectile settings panel.
		void DrawProjectileSettings(const String& id);
		
		/// @brief Draws the particle effect settings panel.
		void DrawParticleSettings(const String& id);
		
		/// @brief Draws the ribbon trail settings panel.
		void DrawRibbonSettings(const String& id);
		
		/// @brief Loads the preview configuration from file.
		bool LoadPreviewConfig();
		
		/// @brief Creates the floor plane mesh.
		void CreateFloorPlane();
		
		/// @brief Creates the caster and target entities.
		void CreateCharacterEntities();
		
		/// @brief Triggers the spell cast sequence.
		void TriggerSpellCast();
		
		/// @brief Updates the projectile simulation.
		void UpdateProjectile(float deltaTime);
		
		/// @brief Resets the spell effect preview.
		void ResetPreview();

	private:
		SpellEffectEditor& m_editor;
		PreviewProviderManager& m_previewManager;
		scoped_connection m_renderConnection;
		
		// Viewport state
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_initDockLayout{ true };
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
		
		Entity* m_targetEntity{ nullptr };
		SceneNode* m_targetNode{ nullptr };
		AnimationState* m_targetAnimState{ nullptr };
		
		// Spell effect objects
		ParticleEmitter* m_castParticles{ nullptr };
		ParticleEmitter* m_impactParticles{ nullptr };
		RibbonTrail* m_projectileTrail{ nullptr };
		Entity* m_projectileEntity{ nullptr };
		SceneNode* m_projectileNode{ nullptr };
		
		// Projectile state
		bool m_projectileActive{ false };
		Vector3 m_projectileStartPos;
		Vector3 m_projectileTargetPos;
		float m_projectileProgress{ 0.0f };
		float m_projectileSpeed{ 15.0f };
		
		// Mouse interaction
		int16 m_lastMouseX{ 0 };
		int16 m_lastMouseY{ 0 };
		bool m_leftButtonPressed{ false };
		bool m_rightButtonPressed{ false };
		bool m_middleButtonPressed{ false };
		
		// Animation timing
		std::chrono::high_resolution_clock::time_point m_lastUpdateTime;
		
		// UI state
		bool m_isPlaying{ false };
		bool m_loopAnimation{ false };
		
		// Configurable paths
		String m_casterMeshPath{ "Models/Characters/Human/Female/HumanFemale_V2.hmsh" };
		String m_targetMeshPath{ "Models/Creatures/Boar/Boar.hmsh" };
		String m_projectileMeshPath;
		String m_castParticlePath;
		String m_impactParticlePath;
		
		// Ribbon trail parameters
		RibbonTrailParameters m_ribbonParams;
		bool m_useRibbonTrail{ true };
		
		// Cast animation
		String m_castAnimationName{ "Attack" };
		String m_targetHitAnimationName{ "Hit" };
		
		// Saved configuration
		uint32 m_visualizationId{ 0 };
	};
}

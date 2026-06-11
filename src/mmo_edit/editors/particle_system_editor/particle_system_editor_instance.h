// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>

#include <chrono>
#include <memory>

#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "editors/color_curve_editor/color_curve_imgui_editor.h"
#include "editors/color_curve_editor/float_curve_imgui_editor.h"

namespace mmo
{
	class ParticleSystemEditor;
	class Camera;
	class SceneNode;

	/// @brief Niagara-inspired multi-emitter particle system editor instance.
	class ParticleSystemEditorInstance final : public EditorInstance
	{
	public:
		explicit ParticleSystemEditorInstance(EditorHost& host, ParticleSystemEditor& editor, Path asset);

		~ParticleSystemEditorInstance() override;

	public:
		void Render();

		void Draw() override;

		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

		bool Save() override;

	private:
		// Window panels
		void DrawViewport(const String& id);
		void DrawPreviewToolbar();
		void DrawEmitterList(const String& id);
		void DrawEmitterStack(const String& id);

		// Module stack sections (operate on the selected emitter)
		void DrawEmitterModule(EmitterParameters& e);
		void DrawEmissionModule(EmitterParameters& e);
		void DrawSpawnModule(EmitterParameters& e);
		void DrawUpdateModule(EmitterParameters& e);
		void DrawRenderModule(EmitterParameters& e);
		void DrawMaterialPicker(EmitterParameters& e);

		// Helpers
		bool LoadParticleSystem();
		void SyncToSystem();
		void MarkDirty() { m_dirty = true; }
		void RestartPreview();
		void ScrubTo(float targetTime);
		void RebuildCurveEditors();
		EmitterParameters MakeTemplate(int templateId, const char* name) const;

		[[nodiscard]] EmitterParameters* SelectedEmitter();

	private:
		ParticleSystemEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Camera* m_camera { nullptr };
		SceneNode* m_systemNode { nullptr };
		ParticleSystem* m_system { nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;

		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_middleButtonPressed { false };
		bool m_initDockLayout { true };

		// Edited data
		ParticleSystemParameters m_systemParams;
		int m_selectedEmitter { 0 };
		bool m_dirty { false };

		// Preview state
		bool m_isPlaying { true };
		bool m_loopPreview { true };
		float m_simSpeed { 1.0f };
		float m_previewTime { 0.0f };
		bool m_wireFrame { false };
		int m_backgroundPreset { 0 };
		bool m_showGrid { true };
		bool m_animateEmitter { false };
		float m_animTime { 0.0f };
		float m_lastFrameMs { 0.0f };
		std::chrono::high_resolution_clock::time_point m_lastFrameTime;
		bool m_frameTimerInit { false };

		// Curve editors (rebound when the selected emitter changes)
		std::unique_ptr<ColorCurveImGuiEditor> m_colorEditor;
		std::unique_ptr<FloatCurveImGuiEditor> m_sizeEditor;
		int m_curveBoundEmitter { -1 };
	};
}

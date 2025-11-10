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
#include "scene_graph/particle_emitter_serializer.h"

namespace mmo
{
	class ParticleSystemEditor;
	class Camera;
	class SceneNode;

	class ParticleSystemEditorInstance final : public EditorInstance
	{
	public:
		explicit ParticleSystemEditorInstance(EditorHost& host, ParticleSystemEditor& editor, Path asset);
		
		~ParticleSystemEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

		bool Save() override;

	private:
		void DrawViewport(const String& id);
		
		void DrawParameters(const String& id);
		
		void DrawEmitterShape(const String& id);
		
		void DrawParticleProperties(const String& id);
		
		void DrawColorCurve(const String& id);
		
		void DrawMaterialSelection(const String& id);
		
		bool LoadParticleSystem();
		
		void UpdateParticleEmitter();

	private:
		ParticleSystemEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Camera* m_camera { nullptr };
		ParticleEmitter* m_emitter { nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;
		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_middleButtonPressed { false };
		bool m_initDockLayout { true };
		ParticleEmitterParameters m_parameters;
		bool m_isPlaying { true };
		bool m_wireFrame { false };
		bool m_parametersDirty { false };
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "particle_system_editor_instance.h"

#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <cmath>

#include "particle_system_editor.h"
#include "editor_host.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene_node.h"
#include "graphics/graphics_device.h"
#include "binary_io/stream_source.h"
#include "binary_io/stream_sink.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo
{
	namespace
	{
		const char* s_shapeNames[] = { "Point", "Sphere", "Box", "Cone" };
		const char* s_simSpaceNames[] = { "World", "Local" };
		const char* s_renderModeNames[] = { "Billboard (Face Camera)", "Velocity Aligned", "Stretched", "Horizontal Billboard" };
		const char* s_spriteAnimNames[] = { "None", "Animate Over Life", "Random Static" };

		// Template ids
		enum TemplateId
		{
			Tpl_Fire = 0,
			Tpl_Smoke,
			Tpl_SparkBurst,
			Tpl_SwirlAura,
			Tpl_Trail,
			Tpl_Slash,
			Tpl_Blood,
			Tpl_HolyColumn,
			Tpl_HealSparkle,
			Tpl_Count
		};

		const char* s_templateNames[] =
		{
			"Fire", "Smoke", "Spark Burst", "Swirl Aura", "Trail",
			"Sword Slash", "Blood Impact", "Holy Column", "Heal Sparkle"
		};
	}

	ParticleSystemEditorInstance::ParticleSystemEditorInstance(EditorHost& host, ParticleSystemEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
	{
		// Camera rig
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetPosition(Vector3::UnitZ * 15.0f);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-25.0f), Vector3::UnitX));
		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_axisDisplay = std::make_unique<AxisDisplay>(m_scene, "DebugAxis");
		m_scene.GetRootSceneNode().AddChild(m_axisDisplay->GetSceneNode());

		// Load the particle system definition (or start with a default).
		if (!LoadParticleSystem())
		{
			WLOG("Failed to load particle system from " << m_assetPath << ", starting with a default emitter.");
			m_systemParams.emitters.clear();
			m_systemParams.emitters.push_back(MakeTemplate(Tpl_Fire, "Emitter"));
		}

		// Create the preview system. The editor drives the simulation manually.
		m_systemNode = m_scene.GetRootSceneNode().CreateChildSceneNode("ParticleSystemNode");
		m_system = m_scene.CreateParticleEmitter("PreviewParticleSystem");
		if (m_system)
		{
			m_system->SetAutoUpdate(false);
			m_system->SetSystemParameters(m_systemParams);
			m_systemNode->AttachObject(*m_system);
			m_system->Play();
		}

		RebuildCurveEditors();

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &ParticleSystemEditorInstance::Render);
	}

	ParticleSystemEditorInstance::~ParticleSystemEditorInstance()
	{
		if (m_system)
		{
			m_scene.DestroyParticleEmitter(*m_system);
		}

		m_worldGrid.reset();
		m_axisDisplay.reset();
		m_scene.Clear();
	}

	EmitterParameters* ParticleSystemEditorInstance::SelectedEmitter()
	{
		if (m_selectedEmitter < 0 || m_selectedEmitter >= static_cast<int>(m_systemParams.emitters.size()))
		{
			return nullptr;
		}
		return &m_systemParams.emitters[m_selectedEmitter];
	}

	void ParticleSystemEditorInstance::RebuildCurveEditors()
	{
		EmitterParameters* e = SelectedEmitter();
		if (!e)
		{
			m_colorEditor.reset();
			m_sizeEditor.reset();
			m_curveBoundEmitter = -1;
			return;
		}

		m_colorEditor = std::make_unique<ColorCurveImGuiEditor>("ColorOverLife", e->colorOverLifetime);
		m_colorEditor->SetShowAlpha(true);
		m_colorEditor->SetShowColorPreview(true);
		m_sizeEditor = std::make_unique<FloatCurveImGuiEditor>("SizeOverLife", e->sizeOverLife);
		m_curveBoundEmitter = m_selectedEmitter;
	}

	void ParticleSystemEditorInstance::SyncToSystem()
	{
		if (!m_system)
		{
			return;
		}

		if (m_system->GetEmitterCount() != m_systemParams.emitters.size())
		{
			m_system->SetSystemParameters(m_systemParams);
			if (m_isPlaying)
			{
				m_system->Play();
			}
			else
			{
				m_system->Stop();
			}
		}
		else
		{
			for (size_t i = 0; i < m_systemParams.emitters.size(); ++i)
			{
				if (EmitterInstance* emitter = m_system->GetEmitter(i))
				{
					emitter->SetParameters(m_systemParams.emitters[i]);
				}
			}
		}
	}

	void ParticleSystemEditorInstance::RestartPreview()
	{
		if (m_system)
		{
			m_system->Reset();
			if (m_isPlaying)
			{
				m_system->Play();
			}
		}
		m_previewTime = 0.0f;
	}

	void ParticleSystemEditorInstance::ScrubTo(float targetTime)
	{
		if (!m_system)
		{
			return;
		}

		m_system->Reset();
		m_system->Play();

		const float step = 1.0f / 60.0f;
		float t = 0.0f;
		int safety = 1200;
		while (t < targetTime && safety-- > 0)
		{
			const float dt = std::min(step, targetTime - t);
			m_system->Update(dt);
			t += dt;
		}

		m_previewTime = targetTime;
		if (!m_isPlaying)
		{
			m_system->Stop();
		}
	}

	void ParticleSystemEditorInstance::Render()
	{
		if (!m_viewportRT) return;
		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f) return;

		// Frame timing
		const auto now = std::chrono::high_resolution_clock::now();
		float dt = 0.0f;
		if (m_frameTimerInit)
		{
			dt = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime).count() / 1000000.0f;
		}
		m_lastFrameTime = now;
		m_frameTimerInit = true;
		m_lastFrameMs = dt * 1000.0f;
		dt = std::min(dt, 0.1f);

		// Apply pending parameter edits to the live system.
		if (m_dirty)
		{
			SyncToSystem();
			m_dirty = false;
		}

		// Optional socket-attach animation to preview local-space simulation following a moving emitter.
		if (m_systemNode)
		{
			if (m_animateEmitter)
			{
				m_animTime += dt * m_simSpeed;
				const float r = 3.0f;
				m_systemNode->SetPosition(Vector3(std::cos(m_animTime) * r, 1.0f + std::sin(m_animTime * 2.0f) * 0.5f, std::sin(m_animTime) * r));
			}
			else
			{
				m_systemNode->SetPosition(Vector3::Zero);
			}
		}

		// Advance the simulation manually (so we control pause / speed).
		if (m_isPlaying && m_system)
		{
			const float scaledDt = dt * m_simSpeed;
			m_system->Update(scaledDt);
			m_previewTime += scaledDt;

			if (m_loopPreview && m_system->IsFinished())
			{
				RestartPreview();
			}
		}

		auto& gx = GraphicsDevice::Get();

		Color clearColor(0.1f, 0.1f, 0.15f, 1.0f);
		if (m_backgroundPreset == 1) clearColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
		else if (m_backgroundPreset == 2) clearColor = Color(0.55f, 0.55f, 0.6f, 1.0f);

		gx.Reset();
		gx.SetClearColor(clearColor);
		m_viewportRT->Activate();
		m_viewportRT->Clear(mmo::ClearFlags::All);
		gx.SetViewport(0, 0, m_lastAvailViewportSize.x, m_lastAvailViewportSize.y, 0.0f, 1.0f);
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		gx.SetFillMode(m_wireFrame ? FillMode::Wireframe : FillMode::Solid);

		m_scene.Render(*m_camera, PixelShaderType::Forward);

		m_viewportRT->Update();
	}

	void ParticleSystemEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockSpaceId = ImGui::GetID("##particle_dockspace_");
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String emittersId = "Emitters##" + GetAssetPath().string();
		const String stackId = "Properties##" + GetAssetPath().string();

		DrawViewport(viewportId);
		DrawEmitterList(emittersId);
		DrawEmitterStack(stackId);

		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockSpaceId;
			const auto sideId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 0.32f, nullptr, &mainId);
			auto topSideId = sideId;
			const auto bottomSideId = ImGui::DockBuilderSplitNode(topSideId, ImGuiDir_Down, 0.7f, nullptr, &topSideId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(emittersId.c_str(), topSideId);
			ImGui::DockBuilderDockWindow(stackId.c_str(), bottomSideId);

			m_initDockLayout = false;
		}

		ImGui::DockBuilderFinish(dockSpaceId);
		ImGui::PopID();
	}

	void ParticleSystemEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			DrawPreviewToolbar();

			const auto availableSpace = ImGui::GetContentRegionAvail();

			if (m_viewportRT == nullptr)
			{
				m_viewportRT = GraphicsDevice::Get().CreateRenderTexture("ParticleViewport",
					std::max(1.0f, availableSpace.x),
					std::max(1.0f, availableSpace.y),
					RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView);
				m_lastAvailViewportSize = availableSpace;
			}
			else if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				m_viewportRT->Resize(std::max(1.0f, availableSpace.x), std::max(1.0f, availableSpace.y));
				m_lastAvailViewportSize = availableSpace;
			}

			ImGui::Image(m_viewportRT->GetTextureObject(), availableSpace);
			ImGui::SetItemUsingMouseWheel();

			if (ImGui::IsItemHovered())
			{
				m_cameraNode->Translate(Vector3::UnitZ * ImGui::GetIO().MouseWheel * 0.5f, TransformSpace::Local);
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_leftButtonPressed = true;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) m_middleButtonPressed = true;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_rightButtonPressed = true;
		}
		ImGui::End();
	}

	void ParticleSystemEditorInstance::DrawPreviewToolbar()
	{
		if (ImGui::Button(m_isPlaying ? "Pause" : "Play"))
		{
			m_isPlaying = !m_isPlaying;
			if (m_system)
			{
				if (m_isPlaying) m_system->Play();
				else m_system->Stop();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart"))
		{
			RestartPreview();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Loop", &m_loopPreview);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("Speed", &m_simSpeed, 0.05f, 3.0f, "%.2fx");

		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::Combo("BG", &m_backgroundPreset, "Dark\0Black\0Light\0");

		ImGui::SameLine();
		ImGui::Checkbox("Wireframe", &m_wireFrame);
		ImGui::SameLine();
		ImGui::Checkbox("Move Emitter", &m_animateEmitter);

		// Scrub bar
		ImGui::SetNextItemWidth(-1.0f);
		float scrub = m_previewTime;
		if (ImGui::SliderFloat("##scrub", &scrub, 0.0f, 5.0f, "t = %.2fs"))
		{
			ScrubTo(scrub);
		}

		// Stats
		const size_t particleCount = m_system ? m_system->GetTotalParticleCount() : 0;
		ImGui::Text("Emitters: %d   Particles: %zu   Frame: %.2f ms",
			static_cast<int>(m_systemParams.emitters.size()), particleCount, m_lastFrameMs);
		ImGui::Separator();
	}

	void ParticleSystemEditorInstance::DrawEmitterList(const String& id)
	{
		if (ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_MenuBar))
		{
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Save", "Ctrl+S"))
					{
						Save();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}

			if (ImGui::Button("+ Add"))
			{
				ImGui::OpenPopup("AddEmitterPopup");
			}
			if (ImGui::BeginPopup("AddEmitterPopup"))
			{
				if (ImGui::MenuItem("Empty Emitter"))
				{
					EmitterParameters e;
					e.name = "Emitter " + std::to_string(m_systemParams.emitters.size() + 1);
					m_systemParams.emitters.push_back(e);
					m_selectedEmitter = static_cast<int>(m_systemParams.emitters.size()) - 1;
					RebuildCurveEditors();
					MarkDirty();
				}
				ImGui::Separator();
				ImGui::TextDisabled("From Template");
				for (int t = 0; t < Tpl_Count; ++t)
				{
					if (ImGui::MenuItem(s_templateNames[t]))
					{
						m_systemParams.emitters.push_back(MakeTemplate(t, s_templateNames[t]));
						m_selectedEmitter = static_cast<int>(m_systemParams.emitters.size()) - 1;
						RebuildCurveEditors();
						MarkDirty();
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Duplicate") && SelectedEmitter())
			{
				EmitterParameters copy = *SelectedEmitter();
				copy.name += " Copy";
				m_systemParams.emitters.insert(m_systemParams.emitters.begin() + m_selectedEmitter + 1, copy);
				m_selectedEmitter++;
				RebuildCurveEditors();
				MarkDirty();
			}

			ImGui::SameLine();
			if (ImGui::Button("Remove") && m_systemParams.emitters.size() > 1 && SelectedEmitter())
			{
				m_systemParams.emitters.erase(m_systemParams.emitters.begin() + m_selectedEmitter);
				m_selectedEmitter = std::min(m_selectedEmitter, static_cast<int>(m_systemParams.emitters.size()) - 1);
				RebuildCurveEditors();
				MarkDirty();
			}

			ImGui::Separator();

			for (int i = 0; i < static_cast<int>(m_systemParams.emitters.size()); ++i)
			{
				EmitterParameters& e = m_systemParams.emitters[i];
				ImGui::PushID(i);

				bool enabled = e.enabled;
				if (ImGui::Checkbox("##enabled", &enabled))
				{
					e.enabled = enabled;
					MarkDirty();
				}
				ImGui::SameLine();

				const bool selected = (i == m_selectedEmitter);
				if (ImGui::Selectable(e.name.empty() ? "(unnamed)" : e.name.c_str(), selected))
				{
					m_selectedEmitter = i;
					RebuildCurveEditors();
				}

				ImGui::PopID();
			}
		}
		ImGui::End();
	}

	void ParticleSystemEditorInstance::DrawEmitterStack(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			EmitterParameters* ep = SelectedEmitter();
			if (!ep)
			{
				ImGui::TextDisabled("No emitter selected.");
				ImGui::End();
				return;
			}

			if (m_curveBoundEmitter != m_selectedEmitter)
			{
				RebuildCurveEditors();
			}

			EmitterParameters& e = *ep;

			if (ImGui::InputText("Name", &e.name))
			{
				MarkDirty();
			}

			if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) DrawEmitterModule(e);
			if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) DrawEmissionModule(e);
			if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen)) DrawSpawnModule(e);
			if (ImGui::CollapsingHeader("Update", ImGuiTreeNodeFlags_DefaultOpen)) DrawUpdateModule(e);
			if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) DrawRenderModule(e);
		}
		ImGui::End();
	}

	void ParticleSystemEditorInstance::DrawEmitterModule(EmitterParameters& e)
	{
		int simSpace = static_cast<int>(e.simulationSpace);
		if (ImGui::Combo("Simulation Space", &simSpace, s_simSpaceNames, IM_ARRAYSIZE(s_simSpaceNames)))
		{
			e.simulationSpace = static_cast<SimulationSpace>(simSpace);
			MarkDirty();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("World: particles stay where spawned (trails, smoke).\nLocal: particles follow the emitter (auras on a moving hand).");
		}

		if (ImGui::Checkbox("Loop", &e.loop)) MarkDirty();
		if (ImGui::DragFloat("Duration (s)", &e.duration, 0.05f, 0.05f, 60.0f)) MarkDirty();
		if (ImGui::DragFloat("Start Delay (s)", &e.startDelay, 0.05f, 0.0f, 60.0f)) MarkDirty();
		if (ImGui::DragFloat("Warmup (s)", &e.warmupTime, 0.05f, 0.0f, 30.0f)) MarkDirty();
		if (ImGui::DragFloat("Inherit Velocity", &e.inheritVelocity, 0.01f, 0.0f, 1.0f)) MarkDirty();
	}

	void ParticleSystemEditorInstance::DrawEmissionModule(EmitterParameters& e)
	{
		if (ImGui::DragFloat("Spawn Rate (/s)", &e.spawnRate, 1.0f, 0.0f, 5000.0f)) MarkDirty();
		if (ImGui::DragInt("Max Particles", reinterpret_cast<int*>(&e.maxParticles), 1.0f, 1, 100000)) MarkDirty();

		ImGui::Separator();
		ImGui::Text("Bursts");
		ImGui::SameLine();
		if (ImGui::SmallButton("+##addburst"))
		{
			EmitterBurst b;
			b.time = 0.0f;
			b.count = 20;
			e.bursts.push_back(b);
			MarkDirty();
		}

		int removeIdx = -1;
		for (int i = 0; i < static_cast<int>(e.bursts.size()); ++i)
		{
			ImGui::PushID(1000 + i);
			ImGui::SetNextItemWidth(90.0f);
			if (ImGui::DragFloat("t", &e.bursts[i].time, 0.01f, 0.0f, e.duration, "%.2fs")) MarkDirty();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			if (ImGui::DragInt("count", reinterpret_cast<int*>(&e.bursts[i].count), 1.0f, 0, 100000)) MarkDirty();
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) removeIdx = i;
			ImGui::PopID();
		}
		if (removeIdx >= 0)
		{
			e.bursts.erase(e.bursts.begin() + removeIdx);
			MarkDirty();
		}
	}

	void ParticleSystemEditorInstance::DrawSpawnModule(EmitterParameters& e)
	{
		int shape = static_cast<int>(e.shape);
		if (ImGui::Combo("Shape", &shape, s_shapeNames, IM_ARRAYSIZE(s_shapeNames)))
		{
			e.shape = static_cast<EmitterShape>(shape);
			MarkDirty();
		}

		switch (e.shape)
		{
		case EmitterShape::Sphere:
			if (ImGui::DragFloat("Radius", &e.shapeExtents.x, 0.05f, 0.0f, 100.0f)) MarkDirty();
			break;
		case EmitterShape::Box:
			if (ImGui::DragFloat3("Extents", &e.shapeExtents.x, 0.05f, 0.0f, 100.0f)) MarkDirty();
			break;
		case EmitterShape::Cone:
		{
			float angleDeg = e.shapeExtents.x * 180.0f / Pi;
			if (ImGui::DragFloat("Angle (deg)", &angleDeg, 1.0f, 0.0f, 180.0f))
			{
				e.shapeExtents.x = angleDeg * Pi / 180.0f;
				MarkDirty();
			}
			if (ImGui::DragFloat("Height", &e.shapeExtents.y, 0.05f, 0.0f, 100.0f)) MarkDirty();
			if (ImGui::DragFloat("Base Radius", &e.shapeExtents.z, 0.05f, 0.0f, 100.0f)) MarkDirty();
			break;
		}
		default:
			ImGui::TextDisabled("Point emitter has no shape parameters.");
			break;
		}

		ImGui::Separator();
		if (ImGui::DragFloatRange2("Lifetime (s)", &e.minLifetime, &e.maxLifetime, 0.05f, 0.0f, 100.0f)) MarkDirty();
		if (ImGui::DragFloatRange2("Start Size", &e.minStartSize, &e.maxStartSize, 0.01f, 0.0f, 100.0f)) MarkDirty();

		if (ImGui::DragFloat3("Min Velocity", &e.minVelocity.x, 0.05f)) MarkDirty();
		if (ImGui::DragFloat3("Max Velocity", &e.maxVelocity.x, 0.05f)) MarkDirty();
		if (ImGui::DragFloatRange2("Start Speed (outward)", &e.minStartSpeed, &e.maxStartSpeed, 0.05f, 0.0f, 200.0f)) MarkDirty();

		float minRotDeg = e.minStartRotation * 180.0f / Pi;
		float maxRotDeg = e.maxStartRotation * 180.0f / Pi;
		if (ImGui::DragFloatRange2("Start Rotation (deg)", &minRotDeg, &maxRotDeg, 1.0f, -360.0f, 360.0f))
		{
			e.minStartRotation = minRotDeg * Pi / 180.0f;
			e.maxStartRotation = maxRotDeg * Pi / 180.0f;
			MarkDirty();
		}

		float minAngVel = e.minAngularVelocity * 180.0f / Pi;
		float maxAngVel = e.maxAngularVelocity * 180.0f / Pi;
		if (ImGui::DragFloatRange2("Rotation Rate (deg/s)", &minAngVel, &maxAngVel, 1.0f, -3600.0f, 3600.0f))
		{
			e.minAngularVelocity = minAngVel * Pi / 180.0f;
			e.maxAngularVelocity = maxAngVel * Pi / 180.0f;
			MarkDirty();
		}
	}

	void ParticleSystemEditorInstance::DrawUpdateModule(EmitterParameters& e)
	{
		if (ImGui::DragFloat3("Gravity", &e.gravity.x, 0.1f)) MarkDirty();
		if (ImGui::DragFloat("Drag", &e.drag, 0.01f, 0.0f, 20.0f)) MarkDirty();

		float orbitalDeg = e.orbitalSpeed * 180.0f / Pi;
		if (ImGui::DragFloat("Orbital/Swirl (deg/s)", &orbitalDeg, 1.0f, -3600.0f, 3600.0f))
		{
			e.orbitalSpeed = orbitalDeg * Pi / 180.0f;
			MarkDirty();
		}
		if (ImGui::DragFloat("Radial Accel", &e.radialAcceleration, 0.05f, -200.0f, 200.0f)) MarkDirty();

		if (ImGui::DragFloat3("Attractor Pos", &e.attractorPosition.x, 0.05f)) MarkDirty();
		if (ImGui::DragFloat("Attractor Strength", &e.attractorStrength, 0.05f, -200.0f, 200.0f)) MarkDirty();

		if (ImGui::DragFloat("Noise Amplitude", &e.noiseAmplitude, 0.05f, 0.0f, 100.0f)) MarkDirty();
		if (ImGui::DragFloat("Noise Frequency", &e.noiseFrequency, 0.01f, 0.0f, 10.0f)) MarkDirty();

		ImGui::Separator();
		ImGui::Text("Size Over Life (multiplier)");
		if (m_sizeEditor && m_sizeEditor->Draw(0.0f, 120.0f))
		{
			MarkDirty();
		}

		ImGui::Separator();
		ImGui::Text("Color Over Life");
		if (m_colorEditor && m_colorEditor->Draw(0.0f, 220.0f))
		{
			MarkDirty();
		}
	}

	void ParticleSystemEditorInstance::DrawRenderModule(EmitterParameters& e)
	{
		int renderMode = static_cast<int>(e.renderMode);
		if (ImGui::Combo("Render Mode", &renderMode, s_renderModeNames, IM_ARRAYSIZE(s_renderModeNames)))
		{
			e.renderMode = static_cast<ParticleRenderMode>(renderMode);
			MarkDirty();
		}

		if (e.renderMode == ParticleRenderMode::Stretched)
		{
			if (ImGui::DragFloat("Length Scale", &e.lengthScale, 0.05f, 1.0f, 50.0f)) MarkDirty();
		}

		ImGui::Separator();
		ImGui::Text("Sprite Sheet");
		if (ImGui::DragInt("Columns", reinterpret_cast<int*>(&e.spriteSheetColumns), 1.0f, 1, 32)) MarkDirty();
		if (ImGui::DragInt("Rows", reinterpret_cast<int*>(&e.spriteSheetRows), 1.0f, 1, 32)) MarkDirty();

		int spriteAnim = static_cast<int>(e.spriteAnimation);
		if (ImGui::Combo("Sprite Animation", &spriteAnim, s_spriteAnimNames, IM_ARRAYSIZE(s_spriteAnimNames)))
		{
			e.spriteAnimation = static_cast<SpriteAnimationMode>(spriteAnim);
			MarkDirty();
		}
		if (e.spriteAnimation == SpriteAnimationMode::AnimateOverLife)
		{
			if (ImGui::DragFloat("Sprite FPS (0=fit life)", &e.spriteAnimationFps, 0.5f, 0.0f, 120.0f)) MarkDirty();
		}

		ImGui::Separator();
		DrawMaterialPicker(e);
	}

	void ParticleSystemEditorInstance::DrawMaterialPicker(EmitterParameters& e)
	{
		ImGui::Text("Material");
		if (ImGui::InputText("##materialName", &e.materialName))
		{
			MarkDirty();
		}

		// Accept a material dropped from the asset browser.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmat"))
			{
				e.materialName = *static_cast<String*>(payload->Data);
				MarkDirty();
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmi"))
			{
				e.materialName = *static_cast<String*>(payload->Data);
				MarkDirty();
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::TextDisabled("Drag a .hmat / .hmi here from the Asset Browser");

		if (ImGui::Button("Additive"))
		{
			e.materialName = "Particles/Additive.hmat";
			MarkDirty();
		}
		ImGui::SameLine();
		if (ImGui::Button("Alpha"))
		{
			e.materialName = "Particles/Alpha.hmat";
			MarkDirty();
		}
		ImGui::SameLine();
		if (ImGui::Button("Soft Additive"))
		{
			e.materialName = "Particles/SoftAdditive.hmat";
			MarkDirty();
		}
	}

	bool ParticleSystemEditorInstance::LoadParticleSystem()
	{
		const auto file = AssetRegistry::OpenFile(m_assetPath.string());
		if (!file)
		{
			ELOG("Failed to open particle system file: " << m_assetPath);
			return false;
		}

		try
		{
			io::StreamSource source(*file);
			io::Reader reader(source);

			ParticleSystemSerializer serializer;
			if (!serializer.Deserialize(m_systemParams, reader))
			{
				ELOG("Failed to deserialize particle system from: " << m_assetPath);
				return false;
			}

			if (m_systemParams.emitters.empty())
			{
				m_systemParams.emitters.push_back(EmitterParameters());
			}

			ILOG("Loaded particle system from: " << m_assetPath << " (" << m_systemParams.emitters.size() << " emitters)");
			return true;
		}
		catch (const std::exception& ex)
		{
			ELOG("Exception while loading particle system from " << m_assetPath << ": " << ex.what());
			return false;
		}
	}

	bool ParticleSystemEditorInstance::Save()
	{
		const auto file = AssetRegistry::CreateNewFile(m_assetPath.string());
		if (!file)
		{
			ELOG("Failed to open particle system file " << m_assetPath << " for writing!");
			return false;
		}

		try
		{
			io::StreamSink sink(*file);
			io::Writer writer(sink);

			ParticleSystemSerializer serializer;
			serializer.Serialize(m_systemParams, writer);

			ILOG("Saved particle system to " << m_assetPath);
			return true;
		}
		catch (const std::exception& ex)
		{
			ELOG("Failed to save particle system: " << ex.what());
			return false;
		}
	}

	EmitterParameters ParticleSystemEditorInstance::MakeTemplate(int templateId, const char* name) const
	{
		EmitterParameters e;
		e.name = name ? name : "Emitter";
		e.materialName = "Particles/Additive.hmat";

		switch (templateId)
		{
		case Tpl_Fire:
			e.shape = EmitterShape::Cone;
			e.shapeExtents = Vector3(0.4f, 0.5f, 0.4f);
			e.spawnRate = 40.0f;
			e.maxParticles = 120;
			e.minLifetime = 0.6f; e.maxLifetime = 1.1f;
			e.minVelocity = Vector3(-0.3f, 1.5f, -0.3f);
			e.maxVelocity = Vector3(0.3f, 3.0f, 0.3f);
			e.gravity = Vector3(0.0f, 1.0f, 0.0f);
			e.minStartSize = 0.5f; e.maxStartSize = 0.9f;
			e.sizeOverLife = FloatCurve(1.0f, 0.1f);
			e.colorOverLifetime = ColorCurve({
				ColorKey(0.0f, Vector4(1.0f, 0.95f, 0.5f, 1.0f)),
				ColorKey(0.5f, Vector4(1.0f, 0.45f, 0.1f, 0.8f)),
				ColorKey(1.0f, Vector4(0.4f, 0.05f, 0.0f, 0.0f)) });
			break;

		case Tpl_Smoke:
			e.materialName = "Particles/Alpha.hmat";
			e.shape = EmitterShape::Sphere;
			e.shapeExtents = Vector3(0.4f, 0.0f, 0.0f);
			e.spawnRate = 12.0f;
			e.maxParticles = 60;
			e.minLifetime = 2.0f; e.maxLifetime = 3.5f;
			e.minVelocity = Vector3(-0.3f, 0.8f, -0.3f);
			e.maxVelocity = Vector3(0.3f, 1.6f, 0.3f);
			e.gravity = Vector3(0.0f, 0.2f, 0.0f);
			e.drag = 0.5f;
			e.minStartSize = 0.6f; e.maxStartSize = 1.0f;
			e.sizeOverLife = FloatCurve(1.0f, 2.5f);
			e.minAngularVelocity = -0.5f; e.maxAngularVelocity = 0.5f;
			e.colorOverLifetime = ColorCurve({
				ColorKey(0.0f, Vector4(0.5f, 0.5f, 0.5f, 0.0f)),
				ColorKey(0.2f, Vector4(0.4f, 0.4f, 0.4f, 0.6f)),
				ColorKey(1.0f, Vector4(0.2f, 0.2f, 0.2f, 0.0f)) });
			break;

		case Tpl_SparkBurst:
			e.shape = EmitterShape::Point;
			e.loop = false;
			e.duration = 0.2f;
			e.spawnRate = 0.0f;
			e.maxParticles = 200;
			e.bursts.push_back({ 0.0f, 60 });
			e.minLifetime = 0.3f; e.maxLifetime = 0.8f;
			e.minVelocity = Vector3(-4.0f, -4.0f, -4.0f);
			e.maxVelocity = Vector3(4.0f, 4.0f, 4.0f);
			e.minStartSpeed = 2.0f; e.maxStartSpeed = 6.0f;
			e.shape = EmitterShape::Sphere;
			e.shapeExtents = Vector3(0.1f, 0.0f, 0.0f);
			e.gravity = Vector3(0.0f, -6.0f, 0.0f);
			e.drag = 1.0f;
			e.minStartSize = 0.1f; e.maxStartSize = 0.2f;
			e.renderMode = ParticleRenderMode::Stretched;
			e.lengthScale = 4.0f;
			e.sizeOverLife = FloatCurve(1.0f, 0.2f);
			e.colorOverLifetime = ColorCurve(Vector4(1.0f, 0.9f, 0.5f, 1.0f), Vector4(1.0f, 0.3f, 0.0f, 0.0f));
			break;

		case Tpl_SwirlAura:
			e.simulationSpace = SimulationSpace::Local;
			e.shape = EmitterShape::Sphere;
			e.shapeExtents = Vector3(0.4f, 0.0f, 0.0f);
			e.spawnRate = 50.0f;
			e.maxParticles = 150;
			e.minLifetime = 0.8f; e.maxLifetime = 1.4f;
			e.minVelocity = Vector3(0.0f, 0.3f, 0.0f);
			e.maxVelocity = Vector3(0.0f, 0.8f, 0.0f);
			e.gravity = Vector3::Zero;
			e.orbitalSpeed = 3.5f;
			e.radialAcceleration = -0.4f;
			e.minStartSize = 0.12f; e.maxStartSize = 0.25f;
			e.sizeOverLife = FloatCurve(0.2f, 1.0f);
			e.colorOverLifetime = ColorCurve({
				ColorKey(0.0f, Vector4(0.4f, 0.7f, 1.0f, 0.0f)),
				ColorKey(0.3f, Vector4(0.5f, 0.8f, 1.0f, 1.0f)),
				ColorKey(1.0f, Vector4(0.8f, 0.9f, 1.0f, 0.0f)) });
			break;

		case Tpl_Trail:
			e.shape = EmitterShape::Point;
			e.spawnRate = 80.0f;
			e.maxParticles = 200;
			e.minLifetime = 0.4f; e.maxLifetime = 0.7f;
			e.minVelocity = Vector3::Zero;
			e.maxVelocity = Vector3::Zero;
			e.gravity = Vector3::Zero;
			e.minStartSize = 0.2f; e.maxStartSize = 0.35f;
			e.sizeOverLife = FloatCurve(1.0f, 0.0f);
			e.colorOverLifetime = ColorCurve(Vector4(0.7f, 0.85f, 1.0f, 1.0f), Vector4(0.3f, 0.5f, 1.0f, 0.0f));
			break;

		case Tpl_Slash:
			e.shape = EmitterShape::Point;
			e.loop = false;
			e.duration = 0.15f;
			e.spawnRate = 0.0f;
			e.maxParticles = 64;
			e.bursts.push_back({ 0.0f, 18 });
			e.minLifetime = 0.18f; e.maxLifetime = 0.28f;
			e.minVelocity = Vector3(6.0f, 0.0f, -2.0f);
			e.maxVelocity = Vector3(10.0f, 0.0f, 2.0f);
			e.gravity = Vector3::Zero;
			e.minStartSize = 0.25f; e.maxStartSize = 0.4f;
			e.renderMode = ParticleRenderMode::Stretched;
			e.lengthScale = 8.0f;
			e.sizeOverLife = FloatCurve(1.0f, 0.3f);
			e.colorOverLifetime = ColorCurve(Vector4(1.0f, 1.0f, 1.0f, 1.0f), Vector4(0.7f, 0.8f, 1.0f, 0.0f));
			break;

		case Tpl_Blood:
			e.materialName = "Particles/Alpha.hmat";
			e.shape = EmitterShape::Sphere;
			e.shapeExtents = Vector3(0.1f, 0.0f, 0.0f);
			e.loop = false;
			e.duration = 0.2f;
			e.spawnRate = 0.0f;
			e.maxParticles = 80;
			e.bursts.push_back({ 0.0f, 30 });
			e.minLifetime = 0.4f; e.maxLifetime = 0.9f;
			e.minVelocity = Vector3(-2.0f, 1.0f, -2.0f);
			e.maxVelocity = Vector3(2.0f, 3.5f, 2.0f);
			e.minStartSpeed = 1.0f; e.maxStartSpeed = 3.0f;
			e.gravity = Vector3(0.0f, -9.81f, 0.0f);
			e.minStartSize = 0.12f; e.maxStartSize = 0.25f;
			e.spriteSheetColumns = 1; e.spriteSheetRows = 1;
			e.sizeOverLife = FloatCurve(1.0f, 0.6f);
			e.colorOverLifetime = ColorCurve(Vector4(0.6f, 0.0f, 0.0f, 1.0f), Vector4(0.25f, 0.0f, 0.0f, 0.0f));
			break;

		case Tpl_HolyColumn:
			e.simulationSpace = SimulationSpace::Local;
			e.shape = EmitterShape::Cone;
			e.shapeExtents = Vector3(0.05f, 0.2f, 0.6f);
			e.spawnRate = 70.0f;
			e.maxParticles = 200;
			e.minLifetime = 0.8f; e.maxLifetime = 1.4f;
			e.minVelocity = Vector3(0.0f, 3.0f, 0.0f);
			e.maxVelocity = Vector3(0.0f, 5.0f, 0.0f);
			e.gravity = Vector3::Zero;
			e.orbitalSpeed = 1.5f;
			e.minStartSize = 0.2f; e.maxStartSize = 0.4f;
			e.renderMode = ParticleRenderMode::VelocityAligned;
			e.sizeOverLife = FloatCurve(0.5f, 1.0f);
			e.colorOverLifetime = ColorCurve({
				ColorKey(0.0f, Vector4(1.0f, 0.95f, 0.6f, 0.0f)),
				ColorKey(0.25f, Vector4(1.0f, 0.95f, 0.7f, 1.0f)),
				ColorKey(1.0f, Vector4(1.0f, 1.0f, 0.9f, 0.0f)) });
			break;

		case Tpl_HealSparkle:
			e.simulationSpace = SimulationSpace::Local;
			e.shape = EmitterShape::Cone;
			e.shapeExtents = Vector3(0.1f, 0.3f, 0.5f);
			e.spawnRate = 45.0f;
			e.maxParticles = 150;
			e.minLifetime = 0.9f; e.maxLifetime = 1.6f;
			e.minVelocity = Vector3(0.0f, 1.5f, 0.0f);
			e.maxVelocity = Vector3(0.0f, 3.0f, 0.0f);
			e.gravity = Vector3(0.0f, 0.5f, 0.0f);
			e.minStartSize = 0.12f; e.maxStartSize = 0.22f;
			e.minAngularVelocity = -2.0f; e.maxAngularVelocity = 2.0f;
			e.sizeOverLife = FloatCurve({ FloatKey(0.0f, 0.0f), FloatKey(0.3f, 1.0f), FloatKey(1.0f, 0.0f) });
			e.colorOverLifetime = ColorCurve(Vector4(0.5f, 1.0f, 0.6f, 1.0f), Vector4(0.8f, 1.0f, 0.7f, 0.0f));
			break;

		default:
			break;
		}

		return e;
	}

	void ParticleSystemEditorInstance::OnMouseButtonDown(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0) m_leftButtonPressed = true;
		if (button == 1) m_rightButtonPressed = true;
		if (button == 2) m_middleButtonPressed = true;
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void ParticleSystemEditorInstance::OnMouseButtonUp(uint32 button, uint16 x, uint16 y)
	{
		if (button == 0) m_leftButtonPressed = false;
		if (button == 1) m_rightButtonPressed = false;
		if (button == 2) m_middleButtonPressed = false;
	}

	void ParticleSystemEditorInstance::OnMouseMoved(uint16 x, uint16 y)
	{
		const int16 deltaX = x - m_lastMouseX;
		const int16 deltaY = y - m_lastMouseY;

		if (m_leftButtonPressed && m_cameraAnchor)
		{
			m_cameraAnchor->Yaw(Degree(-deltaX * 0.5f), TransformSpace::World);
			m_cameraAnchor->Pitch(Degree(deltaY * 0.5f), TransformSpace::Local);
		}

		if (m_rightButtonPressed && m_cameraAnchor)
		{
			const Vector3 panOffset = m_cameraNode->GetOrientation() * Vector3(-deltaX * 0.01f, deltaY * 0.01f, 0.0f);
			m_cameraAnchor->Translate(panOffset);
		}

		if (m_middleButtonPressed && m_cameraNode)
		{
			const Vector3 currentPos = m_cameraNode->GetPosition();
			const float zoomFactor = 1.0f + (deltaY * 0.01f);
			m_cameraNode->SetPosition(currentPos * zoomFactor);
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_collision_editor.h"

#include "editor_windows/editor_imgui_helpers.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "graphics/material.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdio>

namespace mmo
{
	MeshCollisionEditor::MeshCollisionEditor(Scene& scene, Camera& camera, SceneNode& cameraAnchor, MeshPtr mesh, Entity* entity, String assetPath)
		: m_scene(scene)
		, m_camera(camera)
		, m_cameraAnchor(cameraAnchor)
		, m_mesh(std::move(mesh))
		, m_entity(entity)
		, m_assetPath(std::move(assetPath))
	{
		CollisionRecipe recipe;
		collision_recipe_read::Type result = collision_recipe_read::Absent;
		if (LoadMeshCollisionRecipe(m_assetPath, recipe, result))
		{
			if (result == collision_recipe_read::Read)
			{
				m_recipe = std::make_unique<CollisionRecipe>(std::move(recipe));
				m_recipeLoaded = true;
			}
			else if (result == collision_recipe_read::Corrupt)
			{
				WLOG("Mesh " << m_assetPath << " has an unreadable collision recipe; it is ignored and replaced on the next collision edit.");
				m_status = "Stored collision recipe is unreadable and was ignored.";
			}
		}

		m_overlay = std::make_unique<CollisionOverlay>(m_scene, m_scene.GetRootSceneNode());
		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);
		ApplyViewMode();

		// The stored recipe is the source of truth: rebuild the tree from it, without marking it
		// touched (EnsureRecipe would), so an unedited mesh saves exactly what it loaded.
		if (m_recipeLoaded)
		{
			m_lastBake = RebuildMeshCollision(*m_mesh, *m_recipe, &m_renderGeometry, true);
		}
	}

	MeshCollisionEditor::~MeshCollisionEditor()
	{
		m_selection.Clear();
		m_transformWidget.reset();
		m_overlay.reset();
	}

	const CollisionRecipe* MeshCollisionEditor::GetRecipeForSave() const
	{
		return (m_recipeTouched || m_recipeLoaded) ? m_recipe.get() : nullptr;
	}

	CollisionShape* MeshCollisionEditor::GetShape(const uint32 index)
	{
		return m_recipe && index < m_recipe->shapes.size() ? &m_recipe->shapes[index] : nullptr;
	}

	void MeshCollisionEditor::EnsureRecipe()
	{
		m_recipeTouched = true;
		if (m_recipe)
		{
			return;
		}

		// Keep today's collision: take the included submeshes from the tree's face mapping.
		m_recipe = std::make_unique<CollisionRecipe>();
		const AABBTree& tree = m_mesh->GetCollisionTree();
		m_recipe->includedSubMeshes = InferIncludedSubMeshes(tree.GetFaceSubMeshes());
		if (m_recipe->includedSubMeshes.empty())
		{
			if (tree.IsEmpty())
			{
				// No collision so far: shapes alone, nothing from the render mesh.
				m_recipe->useRenderGeometry = false;
			}
			else
			{
				WLOG("Mesh " << m_assetPath << " has a legacy collision tree without submesh mapping; including every submesh.");
				m_recipe->includedSubMeshes = AllSubMeshesWithIndexData(*m_mesh);
			}
		}
	}

	void MeshCollisionEditor::Rebake(const bool buildTree)
	{
		EnsureRecipe();
		m_lastBake = RebuildMeshCollision(*m_mesh, *m_recipe, &m_renderGeometry, buildTree);
		m_overlayDirty = true;
		m_previewPending = !buildTree;

		char buffer[128];
		snprintf(buffer, sizeof(buffer), "Faces: %zu  Cut: %u  Nodes: %zu", m_lastBake.indices.size() / 3, m_lastBake.cutFaces, m_mesh->GetCollisionTree().GetNodes().size());
		m_status = buffer;
	}

	void MeshCollisionEditor::OnShapeChanged(const bool final)
	{
		Rebake(final);
	}

	void MeshCollisionEditor::AddShape(const collision_shape_type::Type type)
	{
		EnsureRecipe();

		const AABB& bounds = m_mesh->GetBounds();
		const Vector3 size = bounds.GetSize();
		const float typical = std::max(0.5f, std::max(size.x, std::max(size.y, size.z)) * 0.25f);

		CollisionShape shape;
		shape.type = type;
		shape.name = String(GetCollisionShapeTypeName(type)) + " " + std::to_string(m_recipe->shapes.size() + 1);
		shape.position = m_cameraAnchor.GetPosition();
		shape.scale = Vector3(typical, typical, typical);
		if (type == collision_shape_type::HelixRamp)
		{
			// Spiral stairs usually fill the mesh: match its footprint and height.
			shape.position = bounds.GetCenter();
			shape.scale = Vector3(std::max(size.x, 0.5f), std::max(size.y, 0.5f), std::max(size.z, 0.5f));
			shape.segments = 48;
		}

		if (!m_recipe->includedSubMeshes.empty())
		{
			shape.surfaceSubMesh = m_recipe->includedSubMeshes.front();
		}

		m_recipe->shapes.push_back(shape);
		Rebake(true);
		SelectShape(static_cast<int32>(m_recipe->shapes.size() - 1));
	}

	void MeshCollisionEditor::DeleteShape(const uint32 index)
	{
		if (!m_recipe || index >= m_recipe->shapes.size())
		{
			return;
		}

		SelectShape(-1);
		m_recipe->shapes.erase(m_recipe->shapes.begin() + index);
		Rebake(true);
	}

	void MeshCollisionEditor::DuplicateShape(const uint32 index)
	{
		if (!m_recipe || index >= m_recipe->shapes.size())
		{
			return;
		}

		CollisionShape copy = m_recipe->shapes[index];
		copy.name += " Copy";
		copy.position += Vector3(0.25f, 0.0f, 0.25f);
		m_recipe->shapes.push_back(copy);
		Rebake(true);
		SelectShape(static_cast<int32>(m_recipe->shapes.size() - 1));
	}

	void MeshCollisionEditor::ApplyViewMode()
	{
		const bool active = IsActive();
		m_overlay->SetVisible(active);
		if (m_entity)
		{
			m_entity->SetVisible(m_viewMode != collision_view_mode::CollisionOnly);
		}

		if (!active)
		{
			SelectShape(-1);
		}

		m_overlayDirty = true;
	}

	void MeshCollisionEditor::SelectShape(const int32 index)
	{
		m_selection.Clear();
		m_selectedShape = (m_recipe && index >= 0 && index < static_cast<int32>(m_recipe->shapes.size())) ? index : -1;
		m_overlayDirty = true;
	}

	void MeshCollisionEditor::Update()
	{
		if (m_pendingDelete >= 0)
		{
			const int32 index = m_pendingDelete;
			m_pendingDelete = -1;
			DeleteShape(static_cast<uint32>(index));
		}

		if (m_pendingDuplicate >= 0)
		{
			const int32 index = m_pendingDuplicate;
			m_pendingDuplicate = -1;
			DuplicateShape(static_cast<uint32>(index));
		}

		// Drags (panel fields or gizmo) only preview; rebuild the tree once the mouse is up.
		if (m_previewPending && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			Rebake(true);
		}

		if (m_overlayDirty && IsActive())
		{
			CollisionOverlayData data;
			if (m_recipe && (m_recipeTouched || m_recipeLoaded))
			{
				data.vertices = &m_lastBake.vertices;
				data.indices = &m_lastBake.indices;
				data.faceShape = &m_lastBake.faceShape;
				for (const uint32 face : m_lastBake.cutFaceIndices)
				{
					for (uint32 corner = 0; corner < 3; ++corner)
					{
						data.cutTriangles.push_back(m_renderGeometry.vertices[m_renderGeometry.indices[face * 3 + corner]]);
					}
				}
			}
			else
			{
				data.vertices = &m_mesh->GetCollisionTree().GetVertices();
				data.indices = &m_mesh->GetCollisionTree().GetIndices();
			}

			data.selectedShape = m_selectedShape;
			data.wireframe = m_wireframe;
			m_overlay->Rebuild(data);
			m_overlayDirty = false;
		}

		m_transformWidget->Update(&m_camera);
	}

	String MeshCollisionEditor::GetSubMeshLabel(const uint16 index) const
	{
		SubMesh& sub = m_mesh->GetSubMesh(index);
		const String material = sub.GetMaterial() ? String(sub.GetMaterial()->GetName()) : String("(No Material)");
		return "#" + std::to_string(index + 1) + ": " + material;
	}

	void MeshCollisionEditor::DrawPanel()
	{
		int viewMode = m_viewMode;
		ImGui::TextUnformatted("View");
		ImGui::SameLine();
		bool viewChanged = ImGui::RadioButton("Off", &viewMode, collision_view_mode::Off);
		ImGui::SameLine();
		viewChanged |= ImGui::RadioButton("Overlay", &viewMode, collision_view_mode::Overlay);
		ImGui::SameLine();
		viewChanged |= ImGui::RadioButton("Collision only", &viewMode, collision_view_mode::CollisionOnly);
		if (viewChanged)
		{
			m_viewMode = static_cast<collision_view_mode::Type>(viewMode);
			ApplyViewMode();
		}

		if (ImGui::Checkbox("Wireframe", &m_wireframe))
		{
			m_overlayDirty = true;
		}

		ImGui::TextDisabled("Green walkable, orange too steep, blue shape, red cut, yellow selected.");
		if (!m_status.empty())
		{
			ImGui::TextUnformatted(m_status.c_str());
		}

		ImGui::Separator();

		if (ImGui::Button("Build Complex"))
		{
			Rebake(true);
		}

		ImGui::SameLine();
		if (DrawDangerButton("Clear"))
		{
			ImGui::OpenPopup("Clear collision?");
		}

		if (ImGui::BeginPopupModal("Clear collision?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Removes the baked collision, all shapes and the included submeshes.");
			if (ImGui::Button("Clear"))
			{
				EnsureRecipe();
				SelectShape(-1);
				m_recipe->shapes.clear();
				m_recipe->includedSubMeshes.clear();
				Rebake(true);
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (ImGui::CollapsingHeader("Meshes To Include", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Show the current state without creating a recipe; the first change creates it.
			std::vector<uint16> included = m_recipe ? m_recipe->includedSubMeshes : InferIncludedSubMeshes(m_mesh->GetCollisionTree().GetFaceSubMeshes());
			bool useRender = m_recipe ? m_recipe->useRenderGeometry : true;
			bool edited = ImGui::Checkbox("Use render geometry", &useRender);

			if (ImGui::Button("Select All"))
			{
				included = AllSubMeshesWithIndexData(*m_mesh);
				edited = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Deselect All"))
			{
				included.clear();
				edited = true;
			}

			for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
			{
				ImGui::PushID(i);
				bool isIncluded = std::find(included.begin(), included.end(), i) != included.end();
				if (ImGui::Checkbox(GetSubMeshLabel(i).c_str(), &isIncluded))
				{
					if (isIncluded)
					{
						included.push_back(i);
					}
					else
					{
						included.erase(std::remove(included.begin(), included.end(), i), included.end());
					}

					edited = true;
				}
				ImGui::PopID();
			}

			if (edited)
			{
				EnsureRecipe();
				m_recipe->useRenderGeometry = useRender;
				m_recipe->includedSubMeshes = included;
				Rebake(true);
			}
		}

		if (ImGui::CollapsingHeader("Shapes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!IsActive())
			{
				ImGui::TextDisabled("Switch the view to Overlay or Collision only to edit shapes.");
			}

			ImGui::BeginDisabled(!IsActive());

			for (uint8 t = 0; t < collision_shape_type::Count_; ++t)
			{
				if (t > 0)
				{
					ImGui::SameLine();
				}

				const auto type = static_cast<collision_shape_type::Type>(t);
				if (ImGui::Button((String("+ ") + GetCollisionShapeTypeName(type)).c_str()))
				{
					AddShape(type);
				}
			}

			if (m_recipe)
			{
				for (size_t i = 0; i < m_recipe->shapes.size(); ++i)
				{
					const CollisionShape& shape = m_recipe->shapes[i];
					const String label = shape.name + (shape.op == collision_shape_op::Cut ? "  [Cut]" : "") + "##shape" + std::to_string(i);
					if (ImGui::Selectable(label.c_str(), m_selectedShape == static_cast<int32>(i)))
					{
						SelectShape(static_cast<int32>(i));
					}
				}

				if (CollisionShape* selected = m_selectedShape >= 0 ? GetShape(static_cast<uint32>(m_selectedShape)) : nullptr)
				{
					ImGui::Separator();
					DrawShapeDetails(*selected);
				}
			}

			ImGui::EndDisabled();
		}
	}

	void MeshCollisionEditor::DrawShapeDetails(CollisionShape& shape)
	{
		bool changed = false;
		const uint32 index = static_cast<uint32>(m_selectedShape);

		if (ImGui::InputText("Name", &shape.name))
		{
			m_recipeTouched = true;
		}

		ImGui::Text("Type: %s", GetCollisionShapeTypeName(shape.type));

		ImGui::BeginDisabled(!CollisionShapeSupportsCut(shape.type));
		int op = shape.op;
		changed |= ImGui::RadioButton("Add", &op, collision_shape_op::Add);
		ImGui::SameLine();
		changed |= ImGui::RadioButton("Cut", &op, collision_shape_op::Cut);
		shape.op = static_cast<collision_shape_op::Type>(op);
		ImGui::EndDisabled();

		if (m_mesh->GetSubMeshCount() > 0 && shape.op == collision_shape_op::Add)
		{
			const uint16 current = std::min<uint16>(shape.surfaceSubMesh, m_mesh->GetSubMeshCount() - 1);
			if (ImGui::BeginCombo("Surface", GetSubMeshLabel(current).c_str()))
			{
				for (uint16 i = 0; i < m_mesh->GetSubMeshCount(); ++i)
				{
					if (ImGui::Selectable(GetSubMeshLabel(i).c_str(), i == current))
					{
						shape.surfaceSubMesh = i;
						changed = true;
					}
				}

				ImGui::EndCombo();
			}
		}

		changed |= ImGui::DragFloat3("Position", &shape.position.x, 0.05f);
		changed |= DrawQuaternionEulerDegreesControl("Rotation", shape.rotation);
		changed |= ImGui::DragFloat3("Size", &shape.scale.x, 0.05f, 0.001f, 10000.0f);

		if (shape.type == collision_shape_type::Cylinder || shape.type == collision_shape_type::HelixRamp)
		{
			int segments = shape.segments;
			if (ImGui::SliderInt("Segments", &segments, 3, 256))
			{
				shape.segments = static_cast<uint16>(segments);
				changed = true;
			}
		}

		if (shape.type == collision_shape_type::HelixRamp)
		{
			changed |= ImGui::SliderFloat("Inner radius", &shape.innerRadius, 0.05f, 0.95f);
			changed |= ImGui::DragFloat("Sweep (deg)", &shape.sweepDegrees, 1.0f, 1.0f, 3600.0f);
			changed |= ImGui::SliderFloat("Thickness", &shape.thickness, 0.001f, 1.0f);
			changed |= ImGui::Checkbox("Clockwise", &shape.clockwise);
		}

		if (shape.type == collision_shape_type::Plane)
		{
			changed |= ImGui::Checkbox("Two-sided", &shape.twoSided);
		}

		if (ImGui::Button("Duplicate"))
		{
			DuplicateShape(index);
			return;
		}

		ImGui::SameLine();
		if (DrawDangerButton("Delete"))
		{
			DeleteShape(index);
			return;
		}

		if (changed)
		{
			// Drags fire every frame: preview while the mouse is held; Update() rebuilds the tree on release.
			Rebake(!ImGui::IsMouseDown(ImGuiMouseButton_Left));
		}
	}
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_editor_instance.h"

#include "prop_kit_editor.h"
#include "editor_host.h"
#include "icon_font.h"
#include "selectable.h"
#include "editor_windows/asset_picker_widget.h"
#include "editors/world_model_editor/panels/world_model_properties_panel.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "graphics/graphics_device.h"
#include "log/default_log_levels.h"
#include "math/matrix4.h"
#include "math/plane.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/light.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/prop_kit_manager.h"
#include "scene_graph/prop_kit_serializer.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/world_model_manager.h"

#include <imgui_internal.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace mmo
{
	namespace
	{
		/// Radius around a light marker that picks the light.
		constexpr float LightPickRadius = 0.35f;

		/// Half size of a light marker cross.
		constexpr float LightMarkerSize = 0.25f;

		// ARGB marker colors.
		constexpr uint32 LightMarkerColor = 0xFFFFD040u;
		constexpr uint32 SelectedMarkerColor = 0xFFFFFFFFu;
		constexpr uint32 SelectedPropColor = 0xFFFF8C1Au;

		/// Builds the transform of a prop in world model space.
		Matrix4 MakePropTransform(const WorldModelMeshRef& prop)
		{
			Matrix4 transform;
			transform.MakeTransform(prop.position, prop.scale, prop.rotation);
			return transform;
		}

		/// Display label of a prop: its name, or the file name of its mesh.
		String GetPropLabel(const WorldModelMeshRef& prop)
		{
			if (!prop.name.empty())
			{
				return prop.name;
			}

			return Path(prop.meshPath).filename().string();
		}

		/// Gizmo handle for one prop of the edited kit. Writes transforms straight into the kit.
		class SelectedKitProp final : public Selectable
		{
		public:
			SelectedKitProp(PropKit& kit, const size_t index, std::function<void()> onChanged, std::function<void()> onRemove)
				: m_kit(kit)
				, m_index(index)
				, m_onChanged(std::move(onChanged))
				, m_onRemove(std::move(onRemove))
			{
			}

		public:
			void Visit(SelectableVisitor& visitor) override {}

			void Duplicate() override {}

			[[nodiscard]] bool SupportsDuplicate() const override { return false; }

			void Translate(const Vector3& delta) override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->position += delta;
					m_onChanged();
					positionChanged(*this);
				}
			}

			void Rotate(const Quaternion& delta) override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->rotation = delta * prop->rotation;
					m_onChanged();
					rotationChanged(*this);
				}
			}

			void Scale(const Vector3& delta) override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->scale += delta;
					m_onChanged();
					scaleChanged(*this);
				}
			}

			void Remove() override
			{
				m_onRemove();
			}

			void Deselect() override {}

			void SetPosition(const Vector3& position) const override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->position = position;
					m_onChanged();
					positionChanged(*this);
				}
			}

			void SetOrientation(const Quaternion& orientation) const override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->rotation = orientation;
					m_onChanged();
					rotationChanged(*this);
				}
			}

			void SetScale(const Vector3& scale) const override
			{
				if (WorldModelMeshRef* prop = GetProp())
				{
					prop->scale = scale;
					m_onChanged();
					scaleChanged(*this);
				}
			}

			[[nodiscard]] Vector3 GetPosition() const override
			{
				const WorldModelMeshRef* prop = GetProp();
				return prop ? prop->position : Vector3::Zero;
			}

			[[nodiscard]] Quaternion GetOrientation() const override
			{
				const WorldModelMeshRef* prop = GetProp();
				return prop ? prop->rotation : Quaternion::Identity;
			}

			[[nodiscard]] Vector3 GetScale() const override
			{
				const WorldModelMeshRef* prop = GetProp();
				return prop ? prop->scale : Vector3::UnitScale;
			}

		private:
			[[nodiscard]] WorldModelMeshRef* GetProp() const
			{
				return m_index < m_kit.props.size() ? &m_kit.props[m_index] : nullptr;
			}

		private:
			PropKit& m_kit;
			size_t m_index;
			std::function<void()> m_onChanged;
			std::function<void()> m_onRemove;
		};

		/// Gizmo handle for one light of the edited kit. Spot lights can also be rotated.
		class SelectedKitLight final : public Selectable
		{
		public:
			SelectedKitLight(PropKit& kit, const size_t index, std::function<void()> onChanged, std::function<void()> onRemove)
				: m_kit(kit)
				, m_index(index)
				, m_onChanged(std::move(onChanged))
				, m_onRemove(std::move(onRemove))
			{
			}

		public:
			void Visit(SelectableVisitor& visitor) override {}

			void Duplicate() override {}

			[[nodiscard]] bool SupportsDuplicate() const override { return false; }

			[[nodiscard]] bool SupportsRotate() const override
			{
				const WorldModelLight* light = GetLight();
				return light && light->type == WorldModelLight::LightType::Spot;
			}

			[[nodiscard]] bool SupportsScale() const override { return false; }

			void Translate(const Vector3& delta) override
			{
				if (WorldModelLight* light = GetLight())
				{
					light->position += delta;
					m_onChanged();
					positionChanged(*this);
				}
			}

			void Rotate(const Quaternion& delta) override
			{
				if (WorldModelLight* light = GetLight())
				{
					light->rotation = delta * light->rotation;
					m_onChanged();
					rotationChanged(*this);
				}
			}

			void Scale(const Vector3& delta) override {}

			void Remove() override
			{
				m_onRemove();
			}

			void Deselect() override {}

			void SetPosition(const Vector3& position) const override
			{
				if (WorldModelLight* light = GetLight())
				{
					light->position = position;
					m_onChanged();
					positionChanged(*this);
				}
			}

			void SetOrientation(const Quaternion& orientation) const override
			{
				if (WorldModelLight* light = GetLight())
				{
					light->rotation = orientation;
					m_onChanged();
					rotationChanged(*this);
				}
			}

			void SetScale(const Vector3& scale) const override {}

			[[nodiscard]] Vector3 GetPosition() const override
			{
				const WorldModelLight* light = GetLight();
				return light ? light->position : Vector3::Zero;
			}

			[[nodiscard]] Quaternion GetOrientation() const override
			{
				const WorldModelLight* light = GetLight();
				return light ? light->rotation : Quaternion::Identity;
			}

			[[nodiscard]] Vector3 GetScale() const override
			{
				return Vector3::UnitScale;
			}

		private:
			[[nodiscard]] WorldModelLight* GetLight() const
			{
				return m_index < m_kit.lights.size() ? &m_kit.lights[m_index] : nullptr;
			}

		private:
			PropKit& m_kit;
			size_t m_index;
			std::function<void()> m_onChanged;
			std::function<void()> m_onRemove;
		};
	}

	PropKitEditorInstance::PropKitEditorInstance(EditorHost& host, PropKitEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
		, m_kit(std::make_shared<PropKit>())
	{
		m_cameraAnchor = &m_scene.CreateSceneNode("CameraAnchor");
		m_cameraNode = &m_scene.CreateSceneNode("CameraNode");
		m_cameraAnchor->AddChild(*m_cameraNode);
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraAnchor->SetOrientation(Quaternion(Degree(-35.0f), Vector3::UnitX));
		m_scene.GetRootSceneNode().AddChild(*m_cameraAnchor);

		m_lightNode = &m_scene.CreateSceneNode("MainLightNode");
		m_scene.GetRootSceneNode().AddChild(*m_lightNode);
		m_mainLight = &m_scene.CreateLight("MainLight", LightType::Directional);
		m_mainLight->SetDirection(Vector3(-0.5f, -1.0f, -0.3f).NormalizedCopy());
		m_mainLight->SetIntensity(1.0f);
		m_mainLight->SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_lightNode->AttachObject(*m_mainLight);

		m_deferredRenderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, 1920, 1080);

		m_worldGrid = std::make_unique<WorldGrid>(m_scene, "WorldGrid");
		m_worldGrid->SetQueryFlags(0);
		m_worldGrid->SetVisible(true);

		m_previewNode = &m_scene.CreateSceneNode("PreviewBuilding");
		m_scene.GetRootSceneNode().AddChild(*m_previewNode);

		m_markers = m_scene.CreateManualRenderObject("PropKitMarkers");
		m_markers->SetCastShadows(false);
		m_markers->SetQueryFlags(0);
		m_markers->SetRenderQueueGroup(Overlay);
		m_scene.GetRootSceneNode().AttachObject(*m_markers);

		m_collisionOverlay = std::make_unique<CollisionOverlay>(m_scene, m_scene.GetRootSceneNode());
		m_collisionOverlay->SetVisible(m_showCollision);

		m_transformWidget = std::make_unique<TransformWidget>(m_selection, m_scene, *m_camera);
		m_transformWidget->SetTransformMode(TransformMode::Translate);

		LoadKit();
		RecreatePreviewInstance();
		FocusPreview();

		m_renderConnection = m_editor.GetHost().beforeUiUpdate.connect(this, &PropKitEditorInstance::Render);
	}

	PropKitEditorInstance::~PropKitEditorInstance()
	{
		m_renderConnection.disconnect();

		m_selection.Clear();
		m_transformWidget.reset();
		m_collisionOverlay.reset();

		DestroyPreviewInstance();

		if (m_markers)
		{
			m_scene.DestroyManualRenderObject(*m_markers);
			m_markers = nullptr;
		}

		m_worldGrid.reset();
		m_scene.Clear();
	}

	void PropKitEditorInstance::LoadKit()
	{
		const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(GetAssetPath().string());
		if (!stream)
		{
			WLOG("Prop kit " << GetAssetPath() << " does not exist yet, starting with an empty kit");
			return;
		}

		io::StreamSource source{ *stream };
		io::Reader reader{ source };

		PropKit loaded;
		PropKitDeserializer deserializer{ loaded };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to read prop kit " << GetAssetPath() << ", starting with an empty kit");
			return;
		}

		*m_kit = std::move(loaded);
		ILOG("Loaded prop kit " << GetAssetPath() << " (" << m_kit->props.size() << " props, " << m_kit->lights.size() << " lights)");
	}

	void PropKitEditorInstance::DestroyPreviewInstance()
	{
		if (!m_previewInstance)
		{
			return;
		}

		m_previewInstance->Destroy(m_scene);
		if (m_previewNode)
		{
			m_previewNode->DetachObject(*m_previewInstance);
		}
		m_previewInstance.reset();
	}

	void PropKitEditorInstance::RecreatePreviewInstance()
	{
		DestroyPreviewInstance();

		m_previewPath = m_kit->previewWorldModel;

		WorldModelPtr model;
		if (!m_previewPath.empty())
		{
			model = WorldModelManager::Get().Load(m_previewPath);
			if (!model)
			{
				WLOG("Prop kit editor: cannot load preview building " << m_previewPath);
			}
		}

		// Without a building the kit is shown on its own, through an empty world model.
		if (!model)
		{
			model = std::make_shared<WorldModel>();
		}

		m_previewInstance = std::make_shared<WorldModelInstance>("PropKitPreview", model);
		m_previewInstance->SetQueryFlags(0);
		m_previewInstance->SetPropKits({ m_kit });
		m_previewNode->AttachObject(*m_previewInstance);

		const uint32 ambientColor = model->GetAmbientColor();
		if ((ambientColor & 0x00FFFFFF) != 0)
		{
			m_scene.SetAmbientColor(Vector3(
				((ambientColor >> 16) & 0xFF) / 255.0f,
				((ambientColor >> 8) & 0xFF) / 255.0f,
				(ambientColor & 0xFF) / 255.0f));
		}
		else
		{
			m_scene.SetAmbientColor(Vector3(0.35f, 0.35f, 0.35f));
		}

		m_previewDirty = false;
		m_markersDirty = true;
		m_collisionDirty = true;
	}

	void PropKitEditorInstance::FocusPreview()
	{
		AABB bounds;
		if (m_previewInstance && m_previewInstance->GetWorldModel())
		{
			bounds = m_previewInstance->GetWorldModel()->GetBoundingBox();
		}

		for (const WorldModelMeshRef& prop : m_kit->props)
		{
			bounds.Combine(prop.position);
		}

		Vector3 center(0.0f, 1.0f, 0.0f);
		float radius = 5.0f;
		if (!bounds.IsNull())
		{
			center = bounds.GetCenter();
			radius = std::max((bounds.max - bounds.min).GetLength() * 0.5f, 2.0f);
		}

		m_cameraAnchor->SetPosition(center);
		m_cameraNode->SetPosition(Vector3(0.0f, 0.0f, radius * 1.6f));
		m_cameraVelocity = Vector3::Zero;
	}

	void PropKitEditorInstance::MarkKitChanged()
	{
		m_previewDirty = true;
		m_markersDirty = true;
		m_collisionDirty = true;
	}

	void PropKitEditorInstance::RebuildMarkers()
	{
		m_markersDirty = false;
		m_markers->Clear();

		const MaterialPtr material = MaterialManager::Get().Load("Editor/Wireframe.hmat");
		if (!material)
		{
			return;
		}

		struct MarkerLine
		{
			Vector3 start;
			Vector3 end;
			uint32 color;
		};
		std::vector<MarkerLine> lines;

		for (size_t i = 0; i < m_kit->lights.size(); ++i)
		{
			const WorldModelLight& light = m_kit->lights[i];
			const bool selected = m_selectedType == PropKitSelectionType::Light && m_selectedIndex == static_cast<int32>(i);
			const uint32 color = selected ? SelectedMarkerColor : LightMarkerColor;
			const float size = selected ? LightMarkerSize * 1.6f : LightMarkerSize;

			lines.push_back({ light.position - Vector3::UnitX * size, light.position + Vector3::UnitX * size, color });
			lines.push_back({ light.position - Vector3::UnitY * size, light.position + Vector3::UnitY * size, color });
			lines.push_back({ light.position - Vector3::UnitZ * size, light.position + Vector3::UnitZ * size, color });

			if (light.type == WorldModelLight::LightType::Spot)
			{
				// Spot lights shine along rotation * +Z (see Light::Update).
				lines.push_back({ light.position, light.position + (light.rotation * Vector3::UnitZ) * (size * 4.0f), color });
			}
		}

		if (m_selectedType == PropKitSelectionType::Prop && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int32>(m_kit->props.size()))
		{
			const WorldModelMeshRef& prop = m_kit->props[m_selectedIndex];
			if (const MeshPtr mesh = MeshManager::Get().Load(prop.meshPath))
			{
				const AABB& box = mesh->GetBounds();
				const Matrix4 transform = MakePropTransform(prop);

				Vector3 corners[8];
				for (int32 c = 0; c < 8; ++c)
				{
					const Vector3 local(
						(c & 1) ? box.max.x : box.min.x,
						(c & 2) ? box.max.y : box.min.y,
						(c & 4) ? box.max.z : box.min.z);
					corners[c] = transform.TransformAffine(local);
				}

				// Edges connect corners that differ in exactly one axis bit.
				for (int32 a = 0; a < 8; ++a)
				{
					for (int32 bit = 1; bit < 8; bit <<= 1)
					{
						if ((a & bit) == 0)
						{
							lines.push_back({ corners[a], corners[a | bit], SelectedPropColor });
						}
					}
				}
			}
		}

		if (lines.empty())
		{
			return;
		}

		auto operation = m_markers->AddLineListOperation(material);
		for (const MarkerLine& line : lines)
		{
			operation->AddLine(line.start, line.end).SetColor(line.color);
		}
	}

	void PropKitEditorInstance::RebuildCollisionOverlay()
	{
		m_collisionDirty = false;
		m_collisionVertices.clear();
		m_collisionIndices.clear();

		for (const WorldModelMeshRef& prop : m_kit->props)
		{
			if (!prop.visible)
			{
				continue;
			}

			const MeshPtr mesh = MeshManager::Get().Load(prop.meshPath);
			if (!mesh)
			{
				continue;
			}

			const AABBTree& tree = mesh->GetCollisionTree();
			const auto& vertices = tree.GetVertices();
			const auto& indices = tree.GetIndices();
			if (vertices.empty() || indices.empty())
			{
				continue;
			}

			const Matrix4 transform = MakePropTransform(prop);
			const uint32 base = static_cast<uint32>(m_collisionVertices.size());
			for (const Vector3& vertex : vertices)
			{
				m_collisionVertices.push_back(transform.TransformAffine(vertex));
			}
			for (const uint32 index : indices)
			{
				m_collisionIndices.push_back(base + index);
			}
		}

		CollisionOverlayData data;
		if (!m_collisionIndices.empty())
		{
			data.vertices = &m_collisionVertices;
			data.indices = &m_collisionIndices;
		}
		m_collisionOverlay->Rebuild(data);
	}

	void PropKitEditorInstance::AddProp(const String& meshPath, const Vector3& position)
	{
		WorldModelMeshRef prop;
		prop.meshPath = meshPath;
		prop.position = position;
		prop.name = Path(meshPath).stem().string();
		m_kit->props.push_back(std::move(prop));

		MarkKitChanged();
		Select(PropKitSelectionType::Prop, static_cast<int32>(m_kit->props.size() - 1));
	}

	void PropKitEditorInstance::AddLight(const WorldModelLight::LightType type)
	{
		WorldModelLight light;
		light.type = type;
		light.useAttenuation = true;
		light.color = 0xFFFFD8A0;
		light.position = m_cameraAnchor->GetPosition();
		light.intensity = 1.0f;
		light.attenuationStart = 0.0f;
		light.attenuationEnd = 6.0f;
		light.fogScattering = 0.0f;

		// Spot lights shine along rotation * +Z; a +90 degree pitch points a fresh one straight down.
		light.rotation = type == WorldModelLight::LightType::Spot
			? Quaternion(Degree(90.0f), Vector3::UnitX)
			: Quaternion::Identity;

		m_kit->lights.push_back(light);

		MarkKitChanged();
		Select(PropKitSelectionType::Light, static_cast<int32>(m_kit->lights.size() - 1));
	}

	void PropKitEditorInstance::RemoveProp(const size_t index)
	{
		if (index >= m_kit->props.size())
		{
			return;
		}

		Select(PropKitSelectionType::None, -1);
		m_kit->props.erase(m_kit->props.begin() + static_cast<std::ptrdiff_t>(index));
		MarkKitChanged();
	}

	void PropKitEditorInstance::RemoveLight(const size_t index)
	{
		if (index >= m_kit->lights.size())
		{
			return;
		}

		Select(PropKitSelectionType::None, -1);
		m_kit->lights.erase(m_kit->lights.begin() + static_cast<std::ptrdiff_t>(index));
		MarkKitChanged();
	}

	void PropKitEditorInstance::Select(const PropKitSelectionType type, const int32 index)
	{
		m_selection.Clear();
		m_selectedType = PropKitSelectionType::None;
		m_selectedIndex = -1;
		m_markersDirty = true;

		// Removal through the selectable is deferred to the end of the frame, because the caller may
		// still be iterating the selection.
		auto onChanged = [this]() { MarkKitChanged(); };

		if (type == PropKitSelectionType::Prop && index >= 0 && index < static_cast<int32>(m_kit->props.size()))
		{
			m_selection.AddSelectable(std::make_unique<SelectedKitProp>(*m_kit, static_cast<size_t>(index), onChanged, [this]() { m_deleteRequested = true; }));
		}
		else if (type == PropKitSelectionType::Light && index >= 0 && index < static_cast<int32>(m_kit->lights.size()))
		{
			m_selection.AddSelectable(std::make_unique<SelectedKitLight>(*m_kit, static_cast<size_t>(index), onChanged, [this]() { m_deleteRequested = true; }));
		}
		else
		{
			return;
		}

		m_selectedType = type;
		m_selectedIndex = index;
	}

	void PropKitEditorInstance::PickAt(const float viewportX, const float viewportY)
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);

		PropKitSelectionType bestType = PropKitSelectionType::None;
		int32 bestIndex = -1;
		float bestDistance = std::numeric_limits<float>::max();

		for (size_t i = 0; i < m_kit->props.size(); ++i)
		{
			const WorldModelMeshRef& prop = m_kit->props[i];
			const MeshPtr mesh = MeshManager::Get().Load(prop.meshPath);
			if (!mesh)
			{
				continue;
			}

			// Test the ray against the mesh bounds in the prop's local space.
			const Matrix4 transform = MakePropTransform(prop);
			if (!transform.IsAffine())
			{
				continue;
			}
			const Matrix4 inverse = transform.InverseAffine();
			const Vector3 localStart = inverse.TransformAffine(ray.origin);
			const Vector3 localEnd = inverse.TransformAffine(ray.destination);
			if (localStart == localEnd)
			{
				continue;
			}

			const Ray localRay(localStart, localEnd);
			const auto [hit, localDistance] = localRay.IntersectsAABB(mesh->GetBounds());
			if (!hit)
			{
				continue;
			}

			const Vector3 hitPoint = transform.TransformAffine(localRay.GetPoint(std::max(localDistance, 0.0f)));
			const float distance = (hitPoint - ray.origin).GetLength();
			if (distance < bestDistance)
			{
				bestDistance = distance;
				bestType = PropKitSelectionType::Prop;
				bestIndex = static_cast<int32>(i);
			}
		}

		// Light markers are small and usually sit inside or on props (lamps), so any light under the
		// mouse wins over props; among lights the nearest one is picked.
		int32 bestLight = -1;
		float bestLightDistance = std::numeric_limits<float>::max();
		for (size_t i = 0; i < m_kit->lights.size(); ++i)
		{
			const Vector3 toLight = m_kit->lights[i].position - ray.origin;
			const float along = toLight.Dot(ray.GetDirection());
			if (along <= 0.0f || along >= bestLightDistance)
			{
				continue;
			}

			const Vector3 closest = ray.origin + ray.GetDirection() * along;
			if ((m_kit->lights[i].position - closest).GetLength() <= LightPickRadius)
			{
				bestLightDistance = along;
				bestLight = static_cast<int32>(i);
			}
		}

		if (bestLight >= 0)
		{
			Select(PropKitSelectionType::Light, bestLight);
			return;
		}

		Select(bestType, bestIndex);
	}

	Vector3 PropKitEditorInstance::GetDropPosition(const float viewportX, const float viewportY) const
	{
		const Ray ray = m_camera->GetCameraToViewportRay(viewportX, viewportY, 10000.0f);
		const Plane ground(Vector3::UnitY, Vector3::Zero);
		const auto hit = ray.Intersects(ground);
		if (hit.first)
		{
			return ray.GetPoint(hit.second);
		}

		return ray.GetPoint(10.0f);
	}

	void PropKitEditorInstance::Render()
	{
		const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;

		if (m_hovering && ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			if (m_selection.IsEmpty())
			{
				FocusPreview();
			}
			else
			{
				m_cameraAnchor->SetPosition(m_selection.GetSelectedObjects().back()->GetPosition());
				m_cameraVelocity = Vector3::Zero;
			}
		}

		if (m_leftButtonPressed || m_rightButtonPressed)
		{
			Vector3 direction = Vector3::Zero;
			if (ImGui::IsKeyDown(ImGuiKey_W))
			{
				direction.z = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_S))
			{
				direction.z = 1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_A))
			{
				direction.x = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_D))
			{
				direction.x = 1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_Q))
			{
				direction.y = -1.0f;
			}
			if (ImGui::IsKeyDown(ImGuiKey_E))
			{
				direction.y = 1.0f;
			}

			if (direction != Vector3::Zero)
			{
				m_cameraVelocity = direction.NormalizedCopy() * m_cameraSpeed;
			}
		}

		m_cameraAnchor->Translate(m_cameraVelocity * deltaTimeSeconds, TransformSpace::Local);
		m_cameraVelocity *= powf(0.025f, deltaTimeSeconds);

		// A changed preview building path (asset picker) needs a new instance; any other kit change
		// only needs the kit content rebuilt.
		if (m_previewPath != m_kit->previewWorldModel)
		{
			RecreatePreviewInstance();
		}
		else if (m_previewDirty && m_previewInstance)
		{
			m_previewInstance->SetPropKits({ m_kit });
			m_previewDirty = false;
		}

		if (m_markersDirty)
		{
			RebuildMarkers();
		}

		if (m_showCollision && m_collisionDirty)
		{
			RebuildCollisionOverlay();
		}

		if (!m_deferredRenderer)
		{
			return;
		}

		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f)
		{
			return;
		}

		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();
		gx.Reset();
		m_camera->SetAspectRatio(m_lastAvailViewportSize.x / m_lastAvailViewportSize.y);
		m_camera->SetFillMode(FillMode::Solid);

		m_deferredRenderer->Render(m_scene, *m_camera);
		m_transformWidget->Update(m_camera);

		gx.RestoreState();
	}

	void PropKitEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().c_str());

		const auto dockspaceId = ImGui::GetID("##propkit_dockspace_");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		const String viewportId = "Viewport##" + GetAssetPath().string();
		const String kitId = "Kit##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();

		if (m_hovering)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_1, false))
			{
				m_transformWidget->SetTransformMode(TransformMode::Translate);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_2, false))
			{
				m_transformWidget->SetTransformMode(TransformMode::Rotate);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_3, false))
			{
				m_transformWidget->SetTransformMode(TransformMode::Scale);
			}
		}

		DrawKitPanel(kitId);
		DrawDetailsPanel(detailsId);
		DrawViewport(viewportId);

		// Deletions requested during the frame (Delete key, delete buttons) are applied here, after
		// every panel stopped referencing the selected item.
		if (m_deleteRequested)
		{
			m_deleteRequested = false;
			if (m_selectedType == PropKitSelectionType::Prop)
			{
				RemoveProp(static_cast<size_t>(m_selectedIndex));
			}
			else if (m_selectedType == PropKitSelectionType::Light)
			{
				RemoveLight(static_cast<size_t>(m_selectedIndex));
			}
		}

		if (m_initDockLayout)
		{
			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
			ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

			auto mainId = dockspaceId;
			const auto rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 350.0f / ImGui::GetMainViewport()->Size.x, nullptr, &mainId);
			const auto leftId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 300.0f / (ImGui::GetMainViewport()->Size.x - 350.0f), nullptr, &mainId);

			ImGui::DockBuilderDockWindow(viewportId.c_str(), mainId);
			ImGui::DockBuilderDockWindow(kitId.c_str(), leftId);
			ImGui::DockBuilderDockWindow(detailsId.c_str(), rightId);
			ImGui::DockBuilderFinish(dockspaceId);
			m_initDockLayout = false;
		}

		ImGui::PopID();
	}

	void PropKitEditorInstance::DrawViewport(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			const auto availableSpace = ImGui::GetContentRegionAvail();
			if (m_lastAvailViewportSize.x != availableSpace.x || m_lastAvailViewportSize.y != availableSpace.y)
			{
				if (m_deferredRenderer && availableSpace.x > 0 && availableSpace.y > 0)
				{
					m_deferredRenderer->Resize(static_cast<uint32>(availableSpace.x), static_cast<uint32>(availableSpace.y));
				}
				m_lastAvailViewportSize = availableSpace;
			}

			if (m_deferredRenderer && m_deferredRenderer->GetFinalRenderTarget())
			{
				ImGui::Image(m_deferredRenderer->GetFinalRenderTarget()->GetTextureObject(), availableSpace);
			}
			ImGui::SetItemUsingMouseWheel();

			m_hovering = ImGui::IsItemHovered();
			if (m_hovering)
			{
				m_cameraSpeed = std::max(std::min(m_cameraSpeed + ImGui::GetIO().MouseWheel * 2.0f, 100.0f), 1.0f);
				m_leftButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
				m_rightButtonPressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);
				m_lastContentRectMin = ImGui::GetWindowPos();

				if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_selectedType != PropKitSelectionType::None)
				{
					m_deleteRequested = true;
				}
			}

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					const String meshPath = *static_cast<String*>(payload->Data);
					const ImVec2 mousePos = ImGui::GetMousePos();
					const ImVec2 windowPos = ImGui::GetWindowPos();
					const float viewportX = m_lastAvailViewportSize.x > 0.0f ? (mousePos.x - windowPos.x) / m_lastAvailViewportSize.x : 0.5f;
					const float viewportY = m_lastAvailViewportSize.y > 0.0f ? (mousePos.y - windowPos.y) / m_lastAvailViewportSize.y : 0.5f;
					AddProp(meshPath, GetDropPosition(viewportX, viewportY));
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::SetItemAllowOverlap();
			ImGui::SetCursorPos(ImVec2(16, 16));

			const TransformMode mode = m_transformWidget->GetTransformMode();
			auto modeButton = [this, mode](const char* label, const TransformMode buttonMode, const char* tooltip)
			{
				const bool active = mode == buttonMode;
				if (active)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
				}
				if (ImGui::Button(label))
				{
					m_transformWidget->SetTransformMode(buttonMode);
				}
				if (active)
				{
					ImGui::PopStyleColor();
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", tooltip);
				}
				ImGui::SameLine();
			};

			modeButton(ICON_FA_ARROWS " Move", TransformMode::Translate, "Move the selection (Shortcut: 1)");
			modeButton(ICON_FA_REFRESH " Rotate", TransformMode::Rotate, "Rotate the selection (Shortcut: 2)");
			modeButton(ICON_FA_EXPAND " Scale", TransformMode::Scale, "Scale the selection (Shortcut: 3)");

			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();

			if (ImGui::Checkbox("Show collision", &m_showCollision))
			{
				m_collisionOverlay->SetVisible(m_showCollision);
				m_collisionDirty = true;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Show the collision of the kit props (green: walkable, orange: too steep)");
			}
			ImGui::SameLine();

			bool showGrid = m_worldGrid->IsVisible();
			if (ImGui::Checkbox("Grid", &showGrid))
			{
				m_worldGrid->SetVisible(showGrid);
			}
		}
		ImGui::End();
	}

	void PropKitEditorInstance::DrawKitPanel(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			if (ImGui::Button(ICON_FA_SAVE " Save", ImVec2(-1, 0)))
			{
				Save();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Kit");
			ImGui::Separator();

			ImGui::Text("Name:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1);
			ImGui::InputText("##kitName", &m_kit->name);

			ImGui::Text("Preview building:");
			if (AssetPickerWidget::Draw("##previewBuilding", m_kit->previewWorldModel, asset_extensions::WorldModels, m_editor.GetPreviewManager()))
			{
				RecreatePreviewInstance();
				FocusPreview();
			}
			if (m_kit->previewWorldModel.empty())
			{
				ImGui::TextDisabled("No preview building: props are shown on their own.");
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Props");
			ImGui::Separator();

			const String propsHeader = String(ICON_FA_CUBE " Props (") + std::to_string(m_kit->props.size()) + ")";
			ImGui::TextUnformatted(propsHeader.c_str());

			if (ImGui::BeginChild("##kitProps", ImVec2(0, 220), true))
			{
				for (size_t i = 0; i < m_kit->props.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					const bool selected = m_selectedType == PropKitSelectionType::Prop && m_selectedIndex == static_cast<int32>(i);
					const String label = String(ICON_FA_CUBE " ") + GetPropLabel(m_kit->props[i]);
					if (ImGui::Selectable(label.c_str(), selected))
					{
						Select(PropKitSelectionType::Prop, static_cast<int32>(i));
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", m_kit->props[i].meshPath.c_str());
					}
					ImGui::PopID();
				}

				if (m_kit->props.empty())
				{
					ImGui::TextDisabled("Drag meshes (.hmsh) from the asset");
					ImGui::TextDisabled("browser into the viewport or here.");
				}
			}
			ImGui::EndChild();

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".hmsh"))
				{
					AddProp(*static_cast<String*>(payload->Data), m_cameraAnchor->GetPosition());
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Lights");
			ImGui::Separator();

			if (ImGui::Button(ICON_FA_PLUS " Point Light"))
			{
				AddLight(WorldModelLight::LightType::Omni);
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_PLUS " Spot Light"))
			{
				AddLight(WorldModelLight::LightType::Spot);
			}

			if (ImGui::BeginChild("##kitLights", ImVec2(0, 160), true))
			{
				for (size_t i = 0; i < m_kit->lights.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					const bool selected = m_selectedType == PropKitSelectionType::Light && m_selectedIndex == static_cast<int32>(i);
					const char* typeName = m_kit->lights[i].type == WorldModelLight::LightType::Spot ? "Spot" : "Point";
					const String label = String(ICON_FA_SUN_O " ") + typeName + " Light " + std::to_string(i);
					if (ImGui::Selectable(label.c_str(), selected))
					{
						Select(PropKitSelectionType::Light, static_cast<int32>(i));
					}
					ImGui::PopID();
				}

				if (m_kit->lights.empty())
				{
					ImGui::TextDisabled("No lights.");
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}

	void PropKitEditorInstance::DrawDetailsPanel(const String& id)
	{
		if (ImGui::Begin(id.c_str()))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

			if (m_selectedType == PropKitSelectionType::Prop && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int32>(m_kit->props.size()))
			{
				WorldModelMeshRef& prop = m_kit->props[m_selectedIndex];
				const uint32 changes = DrawMeshRefProperties(prop);

				bool visible = prop.visible;
				if (ImGui::Checkbox("Visible##kitprop", &visible))
				{
					prop.visible = visible;
					MarkKitChanged();
				}

				ImGui::TextDisabled("Rotate with the gizmo (Shortcut: 2).");

				if ((changes & mesh_ref_edit::Delete) != 0)
				{
					m_deleteRequested = true;
				}
				else if ((changes & ~static_cast<uint32>(mesh_ref_edit::Name)) != 0)
				{
					MarkKitChanged();
				}
			}
			else if (m_selectedType == PropKitSelectionType::Light && m_selectedIndex >= 0 && m_selectedIndex < static_cast<int32>(m_kit->lights.size()))
			{
				const uint32 changes = DrawLightProperties(m_kit->lights[m_selectedIndex], m_selectedIndex);
				if ((changes & light_edit::Delete) != 0)
				{
					m_deleteRequested = true;
				}
				else if (changes != light_edit::None)
				{
					MarkKitChanged();
				}
			}
			else
			{
				ImGui::TextDisabled("No selection");
				ImGui::TextDisabled("Select a prop or light in the kit list");
				ImGui::TextDisabled("or click it in the viewport.");
			}

			ImGui::PopStyleVar(2);
		}
		ImGui::End();
	}

	void PropKitEditorInstance::OnMouseButtonDown(const uint32 button, const uint16 x, const uint16 y)
	{
		m_lastMouseX = static_cast<int16>(x);
		m_lastMouseY = static_cast<int16>(y);
		m_mouseDownX = x;
		m_mouseDownY = y;

		if (m_hovering && m_lastAvailViewportSize.x > 0.0f && m_lastAvailViewportSize.y > 0.0f)
		{
			const auto mousePos = ImGui::GetMousePos();
			m_transformWidget->OnMousePressed(button, (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x, (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
		}
	}

	void PropKitEditorInstance::OnMouseButtonUp(const uint32 button, const uint16 x, const uint16 y)
	{
		const bool widgetWasActive = m_transformWidget->IsActive();

		if (button == 0)
		{
			m_leftButtonPressed = false;
		}
		else if (button == 1)
		{
			m_rightButtonPressed = false;
		}

		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f)
		{
			return;
		}

		const auto mousePos = ImGui::GetMousePos();
		const float viewportX = (mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x;
		const float viewportY = (mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y;
		m_transformWidget->OnMouseReleased(button, viewportX, viewportY);

		// A click (no camera drag) that did not end a gizmo drag selects what is under the mouse.
		const bool dragged = std::abs(static_cast<int32>(x) - m_mouseDownX) > 3 || std::abs(static_cast<int32>(y) - m_mouseDownY) > 3;
		if (m_hovering && button == 0 && !widgetWasActive && !dragged)
		{
			PickAt(viewportX, viewportY);
		}
	}

	void PropKitEditorInstance::OnMouseMoved(const uint16 x, const uint16 y)
	{
		if (!m_transformWidget->IsActive())
		{
			const float deltaTimeSeconds = ImGui::GetCurrentContext()->IO.DeltaTime;
			const int16 deltaX = static_cast<int16>(x) - m_lastMouseX;
			const int16 deltaY = static_cast<int16>(y) - m_lastMouseY;

			if (m_rightButtonPressed || m_leftButtonPressed)
			{
				m_cameraAnchor->Yaw(-Degree(deltaX * 90.0f * deltaTimeSeconds), TransformSpace::World);
				m_cameraAnchor->Pitch(-Degree(deltaY * 90.0f * deltaTimeSeconds), TransformSpace::Local);
			}

			m_lastMouseX = static_cast<int16>(x);
			m_lastMouseY = static_cast<int16>(y);
		}

		if (m_lastAvailViewportSize.x <= 0.0f || m_lastAvailViewportSize.y <= 0.0f)
		{
			return;
		}

		const auto mousePos = ImGui::GetMousePos();
		m_transformWidget->OnMouseMoved(
			(mousePos.x - m_lastContentRectMin.x) / m_lastAvailViewportSize.x,
			(mousePos.y - m_lastContentRectMin.y) / m_lastAvailViewportSize.y);
	}

	bool PropKitEditorInstance::Save()
	{
		// Placements and the PropKitManager cache use registry paths with forward slashes. An asset
		// path built with Path::operator/ carries a backslash on Windows, which would cache the
		// saved kit under a second key that no placement looks up.
		String path = GetAssetPath().string();
		std::replace(path.begin(), path.end(), '\\', '/');

		const std::unique_ptr<std::ostream> stream = AssetRegistry::CreateNewFile(path);
		if (!stream)
		{
			ELOG("Failed to save prop kit " << path << ": unable to open file for writing");
			return false;
		}

		io::StreamSink sink{ *stream };
		io::Writer writer{ sink };

		PropKitSerializer serializer;
		serializer.Serialize(*m_kit, writer);
		sink.Flush();

		// Replace the cached kit with a copy, so worlds pick up the saved state on reload while
		// further unsaved edits here do not leak into them.
		PropKitManager::Get().Remove(path);
		PropKitManager::Get().Insert(path, std::make_shared<PropKit>(*m_kit));

		ILOG("Saved prop kit " << path << " (" << m_kit->props.size() << " props, " << m_kit->lights.size() << " lights)");
		return true;
	}
}

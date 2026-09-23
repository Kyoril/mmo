// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "fog_volume_edit_mode.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "math/degree.h"
#include "math/plane.h"
#include "math/quaternion.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/movable_object.h"

namespace mmo
{
	namespace
	{
		/// Finds a fog volume by id in the given list, or nullptr.
		FogVolume* FindVolumeById(std::vector<FogVolume>& volumes, const uint32 id)
		{
			const auto it = std::find_if(volumes.begin(), volumes.end(), [id](const FogVolume& volume)
			{
				return volume.id == id;
			});

			return it != volumes.end() ? &*it : nullptr;
		}

		/// The label a volume is listed under.
		String GetVolumeLabel(const FogVolume& volume)
		{
			if (!volume.name.empty())
			{
				return volume.name;
			}

			return "Fog Volume #" + std::to_string(volume.id);
		}

		/// Intersects a ray given in the volume's local space with its shape.
		/// @param origin Local-space ray origin.
		/// @param direction Local-space ray direction (unit length; rotation preserves it).
		/// @param halfSize Half extents of the volume.
		/// @param shape The volume's shape.
		/// @param outDistance Distance along the ray of the hit: the entry point, or the exit point
		///        when the ray starts inside, so a smaller volume nested in front still wins.
		/// @return True if the ray hits the shape in front of its origin.
		bool IntersectLocalShape(const Vector3& origin, const Vector3& direction, const Vector3& halfSize, const FogVolumeShape shape, float& outDistance)
		{
			float tNear = -std::numeric_limits<float>::max();
			float tFar = std::numeric_limits<float>::max();

			if (shape == FogVolumeShape::Ellipsoid)
			{
				// Scale the ellipsoid to the unit sphere and solve the quadratic.
				const Vector3 o(origin.x / halfSize.x, origin.y / halfSize.y, origin.z / halfSize.z);
				const Vector3 d(direction.x / halfSize.x, direction.y / halfSize.y, direction.z / halfSize.z);

				const float a = d.Dot(d);
				const float b = 2.0f * o.Dot(d);
				const float c = o.Dot(o) - 1.0f;
				const float discriminant = b * b - 4.0f * a * c;
				if (a <= 0.0f || discriminant < 0.0f)
				{
					return false;
				}

				const float root = std::sqrt(discriminant);
				tNear = (-b - root) / (2.0f * a);
				tFar = (-b + root) / (2.0f * a);
			}
			else
			{
				const float o[3] = { origin.x, origin.y, origin.z };
				const float d[3] = { direction.x, direction.y, direction.z };
				const float h[3] = { halfSize.x, halfSize.y, halfSize.z };

				for (int32 axis = 0; axis < 3; ++axis)
				{
					if (std::abs(d[axis]) < 1.0e-8f)
					{
						if (o[axis] < -h[axis] || o[axis] > h[axis])
						{
							return false;
						}

						continue;
					}

					float t1 = (-h[axis] - o[axis]) / d[axis];
					float t2 = (h[axis] - o[axis]) / d[axis];
					if (t1 > t2)
					{
						std::swap(t1, t2);
					}

					tNear = std::max(tNear, t1);
					tFar = std::min(tFar, t2);
					if (tNear > tFar)
					{
						return false;
					}
				}
			}

			if (tFar < 0.0f)
			{
				return false;
			}

			outDistance = tNear >= 0.0f ? tNear : tFar;
			return true;
		}

		/// Combo labels for FogVolume::noiseDetail (1x, 2x or 4x the zone's noise frequency).
		constexpr const char* s_noiseDetailLabels[] = { "1x", "2x", "4x" };
		constexpr uint8 s_noiseDetailValues[] = { 1, 2, 4 };

		/// Combo labels for FogVolumeShape, indexed by the enum value.
		constexpr const char* s_shapeLabels[] = { "Box", "Ellipsoid" };
	}

	FogVolumeEditMode::FogVolumeEditMode(IWorldEditor& worldEditor)
		: WorldEditMode(worldEditor)
	{
	}

	const char* FogVolumeEditMode::GetName() const
	{
		return "Fog Volumes";
	}

	void FogVolumeEditMode::OnActivate()
	{
		WorldEditMode::OnActivate();

		for (const FogVolume& volume : m_worldEditor.GetFogVolumes())
		{
			m_worldEditor.AddFogVolumeVisual(volume.id, false);
		}
	}

	void FogVolumeEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();

		// Clears the selection before the visuals the selectable references are destroyed.
		m_worldEditor.RemoveAllFogVolumeVisuals();
	}

	void FogVolumeEditMode::DrawDetails()
	{
		ImGui::PushID("FogVolumeEditMode");

		if (ImGui::CollapsingHeader("Create Fog Volume", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Button("Add Box"))
			{
				AddVolume(FogVolumeShape::Box);
			}

			ImGui::SameLine();

			if (ImGui::Button("Add Ellipsoid"))
			{
				AddVolume(FogVolumeShape::Ellipsoid);
			}

			ImGui::TextDisabled("New volumes are placed where the viewport centre points.");
		}

		std::vector<FogVolume>& volumes = m_worldEditor.GetFogVolumes();
		const uint32 selectedId = m_worldEditor.GetSelectedFogVolumeId();

		if (ImGui::CollapsingHeader("Fog Volumes", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Volumes on this map: %d", static_cast<int>(volumes.size()));

			if (ImGui::BeginListBox("##fogVolumes", ImVec2(-1.0f, 160.0f)))
			{
				// Selecting only changes the selection, never the vector, so iterating by index is safe.
				for (size_t i = 0; i < volumes.size(); ++i)
				{
					const FogVolume& volume = volumes[i];

					ImGui::PushID(static_cast<int>(volume.id));
					if (ImGui::Selectable(GetVolumeLabel(volume).c_str(), volume.id == selectedId))
					{
						m_worldEditor.SelectFogVolume(volume.id);
					}
					ImGui::PopID();
				}

				ImGui::EndListBox();
			}
		}

		if (selectedId != 0)
		{
			if (FogVolume* volume = FindVolumeById(volumes, selectedId))
			{
				if (ImGui::CollapsingHeader("Selected Fog Volume", ImGuiTreeNodeFlags_DefaultOpen))
				{
					DrawVolumeProperties(*volume);

					ImGui::Spacing();

					if (ImGui::Button("Delete"))
					{
						// Invalidates `volume`; nothing may touch it afterwards.
						m_worldEditor.RemoveFogVolume(selectedId);
					}
				}
			}
		}

		ImGui::PopID();
	}

	void FogVolumeEditMode::DrawVolumeProperties(FogVolume& volume)
	{
		const FogVolume before = volume;
		bool changed = false;

		ImGui::Text("ID: %u", volume.id);

		char nameBuffer[128];
		const size_t nameLength = std::min(volume.name.size(), sizeof(nameBuffer) - 1);
		std::copy_n(volume.name.data(), nameLength, nameBuffer);
		nameBuffer[nameLength] = '\0';
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
		{
			volume.name = nameBuffer;
			changed = true;
		}

		int shapeIndex = volume.shape == FogVolumeShape::Ellipsoid ? 1 : 0;
		if (ImGui::Combo("Shape", &shapeIndex, s_shapeLabels, IM_ARRAYSIZE(s_shapeLabels)))
		{
			volume.shape = shapeIndex == 1 ? FogVolumeShape::Ellipsoid : FogVolumeShape::Box;
			changed = true;
		}

		changed |= ImGui::DragFloat3("Position", volume.position.Ptr(), 0.1f);
		changed |= ImGui::DragFloat3("Size", volume.size.Ptr(), 0.1f, 0.5f, 100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::DragFloat("Yaw", &volume.yaw, 0.5f, 0.0f, 360.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Separator();

		changed |= ImGui::DragFloat("Density", &volume.density, 0.001f, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::ColorEdit3("Color", volume.color.Ptr(), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		changed |= ImGui::DragFloat("Edge Fade", &volume.edgeFade, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		changed |= ImGui::DragFloat("Height Falloff", &volume.heightFalloff, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Separator();

		changed |= ImGui::SliderFloat("Active From", &volume.activeFrom, 0.0f, 24.0f, "%.1f h");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("equal values = always on");
		}

		changed |= ImGui::SliderFloat("Active To", &volume.activeTo, 0.0f, 24.0f, "%.1f h");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("equal values = always on");
		}

		changed |= ImGui::DragFloat("Fade Hours", &volume.fadeHours, 0.05f, 0.0f, 6.0f, "%.2f h", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Separator();

		changed |= ImGui::DragFloat("Noise Amount", &volume.noiseAmount, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

		int noiseIndex = volume.noiseDetail == 4 ? 2 : (volume.noiseDetail == 2 ? 1 : 0);
		if (ImGui::Combo("Noise Detail", &noiseIndex, s_noiseDetailLabels, IM_ARRAYSIZE(s_noiseDetailLabels)))
		{
			volume.noiseDetail = s_noiseDetailValues[std::clamp(noiseIndex, 0, 2)];
			changed = true;
		}

		if (!changed)
		{
			return;
		}

		// The colour picker's HDR range is open-ended; the renderer allows [0, 2].
		SanitizeFogVolume(volume);

		const bool visualChanged =
			volume.shape != before.shape ||
			volume.position != before.position ||
			volume.size != before.size ||
			volume.yaw != before.yaw ||
			volume.color != before.color;
		if (visualChanged)
		{
			m_worldEditor.RefreshFogVolumeVisual(volume.id);
		}

		m_worldEditor.MarkFogVolumesChanged();
	}

	void FogVolumeEditMode::AddVolume(const FogVolumeShape shape)
	{
		FogVolume volume;
		volume.id = m_worldEditor.GenerateFogVolumeId();
		volume.shape = shape;
		volume.position = ComputePlacementPosition();
		SanitizeFogVolume(volume);

		m_worldEditor.GetFogVolumes().push_back(volume);
		m_worldEditor.MarkFogVolumesChanged();
		m_worldEditor.AddFogVolumeVisual(volume.id, true);
	}

	Vector3 FogVolumeEditMode::ComputePlacementPosition() const
	{
		// Same placement rule as dropping an area trigger into the viewport, aimed at its centre.
		constexpr float viewportX = 0.5f;
		constexpr float viewportY = 0.5f;

		Camera& camera = m_worldEditor.GetCamera();
		const Ray ray = camera.GetCameraToViewportRay(viewportX, viewportY, 10000.0f);

		Vector3 position;
		bool hitFound = false;

		if (Scene* scene = camera.GetScene())
		{
			auto query = scene->CreateRayQuery(ray);
			query->SetSortByDistance(true);
			query->Execute();

			float closestDistance = std::numeric_limits<float>::max();
			for (const auto& entry : query->GetLastResult())
			{
				if (hitFound && entry.distance > closestDistance)
				{
					break;
				}

				if (!entry.movable)
				{
					continue;
				}

				const ICollidable* collidable = entry.movable->GetCollidable();
				if (!collidable || !collidable->IsCollidable())
				{
					continue;
				}

				CollisionResult hit;
				if (collidable->TestRayCollision(ray, hit) && hit.distance < closestDistance)
				{
					closestDistance = hit.distance;
					position = hit.contactPoint;
					hitFound = true;
				}
			}
		}

		if (!hitFound)
		{
			const Plane groundPlane(Vector3::UnitY, Vector3::Zero);
			const auto hit = ray.Intersects(groundPlane);
			position = hit.first ? ray.GetPoint(hit.second) : ray.GetPoint(10.0f);
		}

		if (m_worldEditor.IsGridSnapEnabled())
		{
			const float gridSize = m_worldEditor.GetTranslateGridSnapSize();
			if (gridSize > 0.0f)
			{
				position.x = std::round(position.x / gridSize) * gridSize;
				position.y = std::round(position.y / gridSize) * gridSize;
				position.z = std::round(position.z / gridSize) * gridSize;
			}
		}

		return position;
	}

	uint32 FogVolumeEditMode::PickVolume(const float viewportX, const float viewportY) const
	{
		const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(viewportX, viewportY, 10000.0f);

		uint32 closestId = 0;
		float closestDistance = std::numeric_limits<float>::max();

		for (const FogVolume& volume : m_worldEditor.GetFogVolumes())
		{
			// Inverse of world = Quaternion(Degree(yaw), UnitY) * local + position.
			const Quaternion inverseOrientation(Degree(-volume.yaw), Vector3::UnitY);
			const Vector3 localOrigin = inverseOrientation * (ray.origin - volume.position);
			const Vector3 localDirection = inverseOrientation * ray.GetDirection();

			float distance = 0.0f;
			if (IntersectLocalShape(localOrigin, localDirection, volume.size * 0.5f, volume.shape, distance) && distance < closestDistance)
			{
				closestDistance = distance;
				closestId = volume.id;
			}
		}

		return closestId;
	}
}

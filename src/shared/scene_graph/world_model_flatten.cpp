// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_flatten.h"

#include "mesh_manager.h"
#include "prop_kit_manager.h"
#include "world_model_manager.h"

#include "log/default_log_levels.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <limits>

namespace mmo
{
	namespace
	{
		/// Receives one placed mesh: its path, the transform of the model placing it, its transform
		/// local to that model, its single material override and whether it lies in an interior group.
		using MeshVisitor = std::function<void(const String& meshPath, const FlattenTransform& model, const FlattenTransform& local, const String& materialOverride, bool interior)>;

		/// Receives one light in model space with the transform of its model.
		using LightVisitor = std::function<void(const WorldModelLight& light, const FlattenTransform& model, bool interior)>;

		/// The walk shared by FlattenWorldModelExterior and FlattenWorldModelExtras. Visits every mesh and
		/// light, interior or not, and leaves the decision to the visitors; counts skips into the report
		/// only when skipping is enabled. Child world models in an interior group are not walked at all.
		class WorldModelWalker final
		{
		public:
			WorldModelWalker(const FlattenOptions& options, FlattenReport* report, MeshVisitor onMesh, LightVisitor onLight)
				: m_options(options)
				, m_report(report)
				, m_onMesh(std::move(onMesh))
				, m_onLight(std::move(onLight))
			{
			}

			void Walk(const WorldModel& model, const FlattenTransform& transform, std::vector<const WorldModel*>& chain)
			{
				const bool isTopLevel = chain.empty();

				// Is a model-space point inside a pure interior group? Only meaningful with skipping enabled.
				const auto isInteriorPoint = [this, &model](const Vector3& localPoint)
				{
					if (!m_options.skipInterior)
					{
						return false;
					}

					const int32 groupIndex = FindWorldModelGroupContaining(model, localPoint);
					const WorldModelGroup* group = groupIndex < 0 ? nullptr : model.GetGroup(static_cast<size_t>(groupIndex));
					return group && IsPureInteriorWorldModelGroup(group->GetFlags());
				};

				const auto placeMesh = [this, &transform](const String& meshPath, const Vector3& position, const Quaternion& rotation, const Vector3& scale, const String& materialOverride, const bool interior)
				{
					if (meshPath.empty())
					{
						return;
					}

					if (interior && m_report)
					{
						++m_report->skippedInteriorMeshes;
					}

					if (m_onMesh)
					{
						m_onMesh(meshPath, transform, FlattenTransform{ position, rotation, scale }, materialOverride, interior);
					}
				};

				// Group mesh references
				for (size_t groupIndex = 0; groupIndex < model.GetGroupCount(); ++groupIndex)
				{
					const WorldModelGroup* group = model.GetGroup(groupIndex);
					if (!group)
					{
						continue;
					}

					const bool interior = m_options.skipInterior && IsPureInteriorWorldModelGroup(group->GetFlags());
					if (interior && m_report)
					{
						++m_report->skippedInteriorGroups;
					}

					for (const WorldModelMeshRef& meshRef : group->GetMeshRefs())
					{
						if (meshRef.visible)
						{
							placeMesh(meshRef.meshPath, meshRef.position, meshRef.rotation, meshRef.scale, meshRef.materialOverride, interior);
						}
					}
				}

				// Doodads of the global set and, on the top-level model, the active set
				if (m_options.includeDoodads)
				{
					const auto& doodads = model.GetDoodads();
					const auto& doodadNames = model.GetDoodadNames();
					const auto& doodadSets = model.GetDoodadSets();

					std::vector<uint32> setIndices;
					if (!doodadSets.empty())
					{
						setIndices.push_back(0);
					}

					const uint32 activeSet = isTopLevel ? m_options.doodadSet : 0;
					if (activeSet > 0 && activeSet < doodadSets.size())
					{
						setIndices.push_back(activeSet);
					}

					for (const uint32 setIndex : setIndices)
					{
						const WorldModelDoodadSet& set = doodadSets[setIndex];
						for (uint32 i = 0; i < set.count; ++i)
						{
							const uint32 doodadIndex = set.startIndex + i;
							if (doodadIndex >= doodads.size() || doodads[doodadIndex].nameIndex >= doodadNames.size())
							{
								continue;
							}

							const WorldModelDoodad& doodad = doodads[doodadIndex];
							placeMesh(doodadNames[doodad.nameIndex], doodad.position, doodad.rotation,
								Vector3(doodad.scale, doodad.scale, doodad.scale), String(), isInteriorPoint(doodad.position));
						}
					}
				}

				// The model's own lights
				for (const WorldModelLight& light : model.GetLights())
				{
					if (m_onLight)
					{
						m_onLight(light, transform, isInteriorPoint(light.position));
					}
				}

				// Prop kits dress the top-level placement only
				if (isTopLevel && m_options.includePropKits)
				{
					for (const String& kitPath : m_options.propKitPaths)
					{
						if (kitPath.empty())
						{
							continue;
						}

						// Recorded before loading: a kit that fails to load is still an input.
						if (m_report)
						{
							m_report->propKitPaths.push_back(kitPath);
						}

						const PropKitPtr kit = PropKitManager::Get().Load(kitPath);
						if (!kit)
						{
							WLOG("Flatten: failed to load prop kit " << kitPath);
							continue;
						}

						for (const WorldModelMeshRef& prop : kit->props)
						{
							if (prop.visible)
							{
								placeMesh(prop.meshPath, prop.position, prop.rotation, prop.scale, prop.materialOverride, isInteriorPoint(prop.position));
							}
						}

						for (const WorldModelLight& light : kit->lights)
						{
							if (m_onLight)
							{
								m_onLight(light, transform, isInteriorPoint(light.position));
							}
						}
					}
				}

				// Child world models, with their transforms composed onto ours
				if (model.GetChildRefs().empty())
				{
					return;
				}

				chain.push_back(&model);
				if (chain.size() > m_options.maxChildDepth)
				{
					WLOG("Flatten: world model '" << model.GetName() << "' is nested " << (chain.size() - 1) << " levels deep, not following its child world models");
					if (m_report)
					{
						m_report->skippedChildren += static_cast<uint32>(model.GetChildRefs().size());
					}
					chain.pop_back();
					return;
				}

				for (const WorldModelChildRef& childRef : model.GetChildRefs())
				{
					if (!childRef.visible || childRef.wmoPath.empty())
					{
						continue;
					}

					// A world model placed inside a room is furniture of that room.
					if (isInteriorPoint(childRef.position))
					{
						if (m_report)
						{
							++m_report->skippedInteriorChildren;
						}
						continue;
					}

					// Recorded before loading: a child that fails to load is still an input.
					if (m_report)
					{
						m_report->worldModelPaths.push_back(childRef.wmoPath);
					}

					const WorldModelPtr child = WorldModelManager::Get().Load(childRef.wmoPath);
					if (!child || std::find(chain.begin(), chain.end(), child.get()) != chain.end())
					{
						WLOG("Flatten: skipped child world model " << childRef.wmoPath << " of '" << model.GetName() << "' (missing or nesting itself)");
						if (m_report)
						{
							++m_report->skippedChildren;
						}
						continue;
					}

					Walk(*child, transform.Compose(childRef.position, childRef.rotation, childRef.scale), chain);
				}

				chain.pop_back();
			}

		private:
			const FlattenOptions& m_options;
			FlattenReport* m_report;
			MeshVisitor m_onMesh;
			LightVisitor m_onLight;
		};

		void RunWalk(const WorldModelPtr& model, const FlattenTransform& transform, const FlattenOptions& options, FlattenReport* report, MeshVisitor onMesh, LightVisitor onLight)
		{
			if (!model)
			{
				return;
			}

			WorldModelWalker walker(options, report, std::move(onMesh), std::move(onLight));
			std::vector<const WorldModel*> chain;
			walker.Walk(*model, transform, chain);
		}

		Quaternion NormalizedOrIdentity(Quaternion rotation)
		{
			const float lengthSq = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
			if (lengthSq > 0.001f)
			{
				rotation.Normalize();
				return rotation;
			}

			return Quaternion::Identity;
		}
	}

	FlattenTransform FlattenTransform::FromMatrix(const Matrix4& matrix)
	{
		FlattenTransform transform;
		matrix.Decomposition(transform.position, transform.scale, transform.orientation);
		return transform;
	}

	Matrix4 FlattenTransform::ToMatrix() const
	{
		Matrix4 matrix;
		matrix.MakeTransform(position, scale, orientation);
		return matrix;
	}

	FlattenTransform FlattenTransform::Compose(const Vector3& localPosition, const Quaternion& localOrientation, const Vector3& localScale) const
	{
		// Same order as Node::UpdateFromParentImpl
		FlattenTransform derived;
		derived.orientation = orientation * localOrientation;
		derived.scale = scale * localScale;
		derived.position = TransformPoint(localPosition);
		return derived;
	}

	Vector3 FlattenTransform::TransformPoint(const Vector3& point) const
	{
		return position + orientation * (scale * point);
	}

	bool IsPureInteriorWorldModelGroup(const uint32 groupFlags)
	{
		return (groupFlags & WorldModelGroupFlags::Interior) != 0 && (groupFlags & WorldModelGroupFlags::Exterior) == 0;
	}

	int32 FindWorldModelGroupContaining(const WorldModel& model, const Vector3& localPoint)
	{
		// Same rule as WorldModelInstance::FindGroupContaining: without containment volumes the group
		// AABBs overlap too freely to trust, so nothing is assigned to a room.
		bool anyContainmentVolumes = false;
		for (size_t i = 0; i < model.GetGroupCount() && !anyContainmentVolumes; ++i)
		{
			const WorldModelGroup* group = model.GetGroup(i);
			anyContainmentVolumes = group && !group->GetContainmentVolumes().empty();
		}

		if (!anyContainmentVolumes)
		{
			return -1;
		}

		int32 bestGroupIndex = -1;
		float smallestVolume = std::numeric_limits<float>::max();
		for (size_t i = 0; i < model.GetGroupCount(); ++i)
		{
			const WorldModelGroup* group = model.GetGroup(i);
			if (!group || !group->ContainsPoint(localPoint))
			{
				continue;
			}

			const Vector3 size = group->GetBoundingBox().max - group->GetBoundingBox().min;
			const float volume = size.x * size.y * size.z;
			if (volume < smallestVolume)
			{
				smallestVolume = volume;
				bestGroupIndex = static_cast<int32>(i);
			}
		}

		return bestGroupIndex;
	}

	bool IsParticleSystemPath(const String& path)
	{
		static const String extension = ".hpar";
		if (path.size() < extension.size())
		{
			return false;
		}

		return std::equal(extension.begin(), extension.end(), path.end() - static_cast<std::ptrdiff_t>(extension.size()), [](const char a, const char b)
			{
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
	}

	std::vector<FlattenedMeshRef> FlattenWorldModelExterior(const WorldModelPtr& model, const Matrix4& transform, const FlattenOptions& options, FlattenReport* report)
	{
		return FlattenWorldModelExterior(model, FlattenTransform::FromMatrix(transform), options, report);
	}

	std::vector<FlattenedMeshRef> FlattenWorldModelExterior(const WorldModelPtr& model, const FlattenTransform& transform, const FlattenOptions& options, FlattenReport* report)
	{
		std::vector<FlattenedMeshRef> result;

		RunWalk(model, transform, options, report,
			[&result](const String& meshPath, const FlattenTransform& modelTransform, const FlattenTransform& local, const String& materialOverride, const bool interior)
			{
				if (interior || IsParticleSystemPath(meshPath))
				{
					return;
				}

				FlattenedMeshRef ref;
				ref.meshPath = meshPath;
				ref.world = modelTransform.Compose(local.position, local.orientation, local.scale).ToMatrix();
				if (!materialOverride.empty())
				{
					ref.materialOverrides.push_back(materialOverride);
				}
				result.push_back(std::move(ref));
			},
			nullptr);

		return result;
	}

	FlattenedWorldModelExtras FlattenWorldModelExtras(const WorldModelPtr& model, const Matrix4& transform, const FlattenOptions& options)
	{
		return FlattenWorldModelExtras(model, FlattenTransform::FromMatrix(transform), options);
	}

	FlattenedWorldModelExtras FlattenWorldModelExtras(const WorldModelPtr& model, const FlattenTransform& transform, const FlattenOptions& options)
	{
		FlattenedWorldModelExtras extras;

		RunWalk(model, transform, options, nullptr,
			[&extras](const String& meshPath, const FlattenTransform& modelTransform, const FlattenTransform& local, const String&, const bool interior)
			{
				if (IsParticleSystemPath(meshPath))
				{
					if (!interior)
					{
						extras.particles.push_back(FlattenedParticles{ meshPath, modelTransform.Compose(local.position, local.orientation, local.scale).ToMatrix() });
					}
					return;
				}

				extras.meshPaths.push_back(meshPath);
				const MeshPtr mesh = MeshManager::Get().Load(meshPath);
				if (!mesh)
				{
					return;
				}

				// Point by point like WorldModelInstance::CollectSoundEmitters: the emitter placed by the
				// mesh transform in model space, then by the model's placement.
				for (const MeshSoundEmitter& emitter : mesh->GetSoundEmitters())
				{
					const Vector3 inModel = local.TransformPoint(emitter.offset);
					extras.sounds.push_back(PlacedSoundEmitter{ emitter.soundEntryId, modelTransform.TransformPoint(inModel), emitter.volume, emitter.maxDistance });
				}
			},
			[&extras](const WorldModelLight& light, const FlattenTransform& modelTransform, const bool interior)
			{
				// Ambient lights become no scene light (WorldModelInstance::CreateLightFrom)
				if (interior || light.type == WorldModelLight::LightType::Ambient)
				{
					return;
				}

				WorldModelLight placed = light;
				placed.position = modelTransform.TransformPoint(light.position);
				placed.rotation = NormalizedOrIdentity(modelTransform.orientation * light.rotation);
				extras.lights.push_back(placed);
			});

		return extras;
	}
}

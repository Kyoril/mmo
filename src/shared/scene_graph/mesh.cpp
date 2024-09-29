// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "mesh.h"
#include "graphics/graphics_device.h"

#include <memory>
#include <ranges>

#include "skeleton_mgr.h"
#include "graphics/vertex_index_data.h"
#include "log/default_log_levels.h"


namespace mmo
{
	SubMesh & Mesh::CreateSubMesh()
	{
		auto subMesh = std::make_unique<SubMesh>(*this);
		m_subMeshes.emplace_back(std::move(subMesh));

		return *m_subMeshes.back();
	}

	SubMesh & Mesh::CreateSubMesh(const std::string & name)
	{
		auto& mesh = CreateSubMesh();
		NameSubMesh(m_subMeshes.size() - 1, name);

		return mesh;
	}

	void Mesh::NameSubMesh(const uint16 index, const std::string & name)
	{
		m_subMeshNames[name] = index;
	}

	bool Mesh::GetSubMeshName(const uint16 index, String& name) const
	{
		for (const auto& [subMeshName, subMeshIndex] : m_subMeshNames)
		{
			if (subMeshIndex == index)
			{
				name = subMeshName;
				return true;
			}
		}

		return false;
	}

	SubMesh & Mesh::GetSubMesh(const uint16 index) const
	{
		return *m_subMeshes[index];
	}

	SubMesh* Mesh::GetSubMesh(const std::string & name)
	{
		if (const auto index = m_subMeshNames.find(name); index != m_subMeshNames.end())
		{
			return &GetSubMesh(index->second);
		}

		return nullptr;
	}

	void Mesh::DestroySubMesh(const uint16 index)
	{
		// Get iterator and advance
		auto subMeshIt = m_subMeshes.begin();
		if (index > 0)
		{
			std::advance(subMeshIt, index);
		}

		// Remove sub mesh at the given index
		m_subMeshes.erase(subMeshIt);

		// Fix name index map
		for (auto it = m_subMeshNames.begin(); it != m_subMeshNames.end(); ++it)
		{
			if (it->second == index)
			{
				m_subMeshNames.erase(it);
				break;
			}
			
			if (it->second > index)
			{
				it->second--;
			}
		}
	}

	void Mesh::DestroySubMesh(const std::string & name)
	{
		if (const auto index = m_subMeshNames.find(name); index != m_subMeshNames.end())
		{
			DestroySubMesh(index->second);
			m_subMeshNames.erase(index);
		}
	}

	void Mesh::SetBounds(const AABB& bounds)
	{
		m_aabb = bounds;
		m_boundRadius = GetBoundingRadiusFromAABB(m_aabb);
	}
	
	void Mesh::SetSkeletonName(const String& skeletonName)
	{
		if (m_skeletonName == skeletonName)
		{
			return;
		}

		m_skeletonName = skeletonName;
		if (m_skeletonName.empty())
		{
			m_skeleton.reset();
		}
		else
		{
			m_skeleton = SkeletonMgr::Get().Load(m_skeletonName + ".skel");
			if (!m_skeleton)
			{
				WLOG("Failed to load skeleton '" << m_skeletonName << "' for mesh '" << m_name << "' - mesh will not be animated!");
				return;
			}

			m_boneMatrices.resize(m_skeleton->GetNumBones());
			m_skeleton->GetBoneMatrices(m_boneMatrices.data());
			m_boneMatricesBuffer = GraphicsDevice::Get().CreateConstantBuffer(sizeof(Matrix4) * m_skeleton->GetNumBones(), m_boneMatrices.data());
		}
	}

	void Mesh::SetSkeleton(SkeletonPtr& skeleton)
	{
		m_skeleton = skeleton;
		m_skeletonName = skeleton ? skeleton->GetName() : "";

		m_boneMatrices.resize(m_skeleton->GetNumBones());
		m_skeleton->GetBoneMatrices(m_boneMatrices.data());
		m_boneMatricesBuffer = GraphicsDevice::Get().CreateConstantBuffer(sizeof(Matrix4) * m_skeleton->GetNumBones(), m_boneMatrices.data());
	}

	void Mesh::AddBoneAssignment(const VertexBoneAssignment& vertBoneAssign)
	{
		m_boneAssignments.insert(VertexBoneAssignmentList::value_type(vertBoneAssign.vertexIndex, vertBoneAssign));
		m_boneAssignmentsOutOfDate = true;
	}

	void Mesh::ClearBoneAssignments()
	{
		m_boneAssignments.clear();
		m_boneAssignmentsOutOfDate = true;
	}

	void Mesh::NotifySkeleton(const SkeletonPtr& skeleton)
	{
		m_skeleton = skeleton;
		m_skeletonName = skeleton ? skeleton->GetName() : "";
	}

	constexpr uint16 MaxBlendWeights = 4;

	typedef std::multimap<float, Mesh::VertexBoneAssignmentList::iterator> WeightIteratorMap;

	uint16 Mesh::NormalizeBoneAssignments(uint64 vertexCount, VertexBoneAssignmentList& assignments) const
	{
		// Iterate through, finding the largest # bones per vertex
		unsigned short maxBones = 0;
		bool hasNonSkinnedVertices = false;
		VertexBoneAssignmentList::iterator i;

		for (size_t v = 0; v < vertexCount; ++v)
		{
			// Get number of entries for this vertex
			short currBones = static_cast<unsigned short>(assignments.count(v));
			if (currBones <= 0)
			{
				hasNonSkinnedVertices = true;
			}
			
			// Deal with max bones update
			// (note this will record maxBones even if they exceed limit)
			if (maxBones < currBones)
			{
				maxBones = currBones;
			}
				
			// does the number of bone assignments exceed limit?
			if (currBones > MaxBlendWeights)
			{
				// To many bone assignments on this vertex
				// Find the start & end (end is in iterator terms ie exclusive)
				std::pair<VertexBoneAssignmentList::iterator, VertexBoneAssignmentList::iterator> range;

				// map to sort by weight
				WeightIteratorMap weightToAssignmentMap;
				range = assignments.equal_range(v);

				// Add all the assignments to map
				for (i = range.first; i != range.second; ++i)
				{
					// insert value weight->iterator
					weightToAssignmentMap.insert(
						WeightIteratorMap::value_type(i->second.weight, i));
				}

				// Reverse iterate over weight map, remove lowest n
				unsigned short numToRemove = currBones - MaxBlendWeights;
				auto remIt = weightToAssignmentMap.begin();

				while (numToRemove--)
				{
					// Erase this one
					assignments.erase(remIt->second);
					++remIt;
				}
			}

			std::pair<VertexBoneAssignmentList::iterator, VertexBoneAssignmentList::iterator> normalise_range = assignments.equal_range(v);
			float totalWeight = 0;

			// Find total first
			for (i = normalise_range.first; i != normalise_range.second; ++i)
			{
				totalWeight += i->second.weight;
			}

			// Now normalise if total weight is outside tolerance
			if (fabs(1.0f - totalWeight) >= FLT_EPSILON)
			{
				for (i = normalise_range.first; i != normalise_range.second; ++i)
				{
					i->second.weight = i->second.weight / totalWeight;
				}
			}
		}

		if (maxBones > MaxBlendWeights)
		{
			WLOG("Mesh " << m_name << " includes vertices with more than " << MaxBlendWeights << " bone assignments. The lowest bone assignments beyong this limit have been removed!");
			maxBones = MaxBlendWeights;
		}

		if (hasNonSkinnedVertices)
		{
			ELOG("Mesh " << m_name << " includes vertices without bone assignments, which will produce errors in animations as those will not be transformed at all!");
		}

		return maxBones;
	}

	void Mesh::CompileBoneAssignments()
	{
		if (sharedVertexData)
		{
			if (const uint16 maxBones = NormalizeBoneAssignments(sharedVertexData->vertexCount, m_boneAssignments); maxBones != 0)
			{
				CompileBoneAssignments(m_boneAssignments, maxBones, sharedBlendIndexToBoneIndexMap, sharedVertexData.get());
			}
		}

		m_boneAssignmentsOutOfDate = false;
	}

	void Mesh::UpdateCompiledBoneAssignments()
	{
		if (m_boneAssignmentsOutOfDate)
		{
			CompileBoneAssignments();
		}

		for (const auto& subMesh : m_subMeshes)
		{
			if (subMesh->m_boneAssignmentsOutOfDate)
			{
				subMesh->CompileBoneAssignments();
			}
		}
	}

	void Mesh::InitAnimationState(AnimationStateSet& animationState)
	{
		if (m_skeleton)
		{
			m_skeleton->InitAnimationState(animationState);
			UpdateCompiledBoneAssignments();
		}
	}

	void Mesh::BuildIndexMap(const VertexBoneAssignmentList& boneAssignments, IndexMap& boneIndexToBlendIndexMap, IndexMap& blendIndexToBoneIndexMap)
	{
		if (boneAssignments.empty())
		{
			// Just in case
			boneIndexToBlendIndexMap.clear();
			blendIndexToBoneIndexMap.clear();
			return;
		}

		typedef std::set<uint16> BoneIndexSet;
		BoneIndexSet usedBoneIndices;

		// Collect actually used bones
		for (const auto& [vertexIndex, boneIndex, weight] : boneAssignments | std::views::values)
		{
			usedBoneIndices.insert(boneIndex);
		}

		// Allocate space for index map
		blendIndexToBoneIndexMap.resize(usedBoneIndices.size());
		boneIndexToBlendIndexMap.resize(*usedBoneIndices.rbegin() + 1);

		// Make index map between bone index and blend index
		unsigned short blendIndex = 0;
		for (auto itBoneIndex = usedBoneIndices.begin(); itBoneIndex != usedBoneIndices.end(); ++itBoneIndex, ++blendIndex)
		{
			boneIndexToBlendIndexMap[*itBoneIndex] = blendIndex;
			blendIndexToBoneIndexMap[blendIndex] = *itBoneIndex;
		}
	}

	void Mesh::CompileBoneAssignments(const VertexBoneAssignmentList& boneAssignments, const uint16 numBlendWeightsPerVertex, IndexMap& blendIndexToBoneIndexMap, const VertexData* targetVertexData)
	{
		// Create or reuse blend weight / indexes buffer
		// Indices are always a UBYTE4 no matter how many weights per vertex
		// Weights are more specific though since they are floats
		VertexDeclaration* decl = targetVertexData->vertexDeclaration;
		VertexBufferBinding* bind = targetVertexData->vertexBufferBinding;
		unsigned short bindIndex;

		IndexMap boneIndexToBlendIndexMap;
		BuildIndexMap(boneAssignments, boneIndexToBlendIndexMap, blendIndexToBoneIndexMap);

		if (const VertexElement* testElem = decl->FindElementBySemantic(VertexElementSemantic::BlendIndices))
		{
			// Already have a buffer, unset it & delete elements
			bindIndex = testElem->GetSource();
			// unset will cause deletion of buffer
			bind->UnsetBinding(bindIndex);
			decl->RemoveElement(VertexElementSemantic::BlendIndices);
			decl->RemoveElement(VertexElementSemantic::BlendWeights);
		}
		else
		{
			bindIndex = bind->GetNextIndex();
		}

		const VertexBufferPtr vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(targetVertexData->vertexCount, sizeof(unsigned char) * 4 + sizeof(float) * numBlendWeightsPerVertex, BufferUsage::DynamicWriteOnlyDiscardable);
		bind->SetBinding(bindIndex, vertexBuffer);

		const VertexElement* pIdxElem, * pWeightElem;

		// add new vertex elements
		if (const VertexElement* firstElem = decl->GetElement(0); firstElem->GetSemantic() == VertexElementSemantic::Position)
		{
			unsigned short insertPoint = 1;
			while (insertPoint < decl->GetElementCount() && decl->GetElement(insertPoint)->GetSource() == firstElem->GetSource())
			{
				++insertPoint;
			}

			const VertexElement& idxElem = decl->InsertElement(insertPoint, bindIndex, 0, VertexElementType::UByte4, VertexElementSemantic::BlendIndices);
			const VertexElement& wtElem =
				decl->InsertElement(insertPoint + 1, bindIndex, sizeof(unsigned char) * 4, VertexElement::MultiplyTypeCount(VertexElementType::Float1, numBlendWeightsPerVertex), VertexElementSemantic::BlendWeights);
			pIdxElem = &idxElem;
			pWeightElem = &wtElem;
		}
		else
		{
			const VertexElement& idxElem =
				decl->AddElement(bindIndex, 0, VertexElementType::UByte4, VertexElementSemantic::BlendIndices);
			const VertexElement& wtElem =
				decl->AddElement(bindIndex, sizeof(unsigned char) * 4,
					VertexElement::MultiplyTypeCount(VertexElementType::Float1, numBlendWeightsPerVertex),
					VertexElementSemantic::BlendWeights);
			pIdxElem = &idxElem;
			pWeightElem = &wtElem;
		}

		// Assign data
		auto i = boneAssignments.begin();
		const auto end = boneAssignments.end();
		auto pBase = static_cast<unsigned char*>(vertexBuffer->Map(LockOptions::Discard));

		// Iterate by vertex
		float* pWeight;
		uint8* pIndex;
		for (size_t v = 0; v < targetVertexData->vertexCount; ++v)
		{
			/// Convert to specific pointers
			pWeightElem->BaseVertexPointerToElement(pBase, &pWeight);
			pIdxElem->BaseVertexPointerToElement(pBase, &pIndex);

			for (uint16 bone = 0; bone < numBlendWeightsPerVertex; ++bone)
			{
				// Do we still have data for this vertex?
				if (i != end && i->second.vertexIndex == v)
				{
					// If so, write weight
					*pWeight++ = i->second.weight;
					*pIndex++ = static_cast<unsigned char>(i->second.boneIndex) + 1;
					++i;
				}
				else
				{
					// Ran out of assignments for this vertex, use weight 0 to indicate empty.
					// If no bones are defined (an error in itself) set bone 0 as the assigned bone. 
					*pWeight++ = (bone == 0) ? 1.0f : 0.0f;
					*pIndex++ = 0;
				}
			}
			pBase += vertexBuffer->GetVertexSize();
		}

		vertexBuffer->Unmap();
	}
}

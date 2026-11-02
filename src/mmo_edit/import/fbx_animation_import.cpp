// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "fbx_animation_import.h"

#include <set>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "binary_io/writer.h"
#include "editors/mesh_editor/anim_evaluator.h"
#include "log/default_log_levels.h"
#include "scene_graph/animation.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/skeleton_mgr.h"
#include "scene_graph/skeleton_serializer.h"

namespace mmo
{
	namespace
	{
		aiMatrix4x4 GetBindTransform(const Node& node)
		{
			const Vector3& position = node.GetPosition();
			const Vector3& scale = node.GetScale();
			const Quaternion& orientation = node.GetOrientation();
			return aiMatrix4x4(
				aiVector3D(scale.x, scale.y, scale.z),
				aiQuaternion(orientation.w, orientation.x, orientation.y, orientation.z),
				aiVector3D(position.x, position.y, position.z));
		}
	}

	int32 ImportFbxAnimations(Skeleton& skeleton, const std::filesystem::path& file, const FbxAnimationNameResolver& resolveName, const float scale)
	{
		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		const aiScene* scene = importer.ReadFile(file.string().c_str(), aiProcess_SortByPType);
		if (!scene)
		{
			ELOG("Failed to open FBX file: " << importer.GetErrorString());
			return -1;
		}

		if (!scene->HasAnimations())
		{
			ELOG("FBX file has no animation data!");
			return 0;
		}

		skeleton.Reset();

		int32 imported = 0;
		DLOG("Scene has " << scene->mNumAnimations << " animations");
		for (uint32 i = 0; i < scene->mNumAnimations; ++i)
		{
			const aiAnimation* anim = scene->mAnimations[i];
			const String animationName = resolveName(anim->mName.C_Str());
			if (animationName.empty())
			{
				continue;
			}

			const double ticksPerSecond = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 30.0;
			ILOG("Importing take '" << anim->mName.C_Str() << "' as '" << animationName << "' (" << anim->mNumChannels << " channels, " << (anim->mDuration / ticksPerSecond) << " seconds)");

			AnimEvaluator evaluator(anim);

			if (skeleton.HasAnimation(animationName))
			{
				skeleton.RemoveAnimation(animationName);
			}

			Animation& animation = skeleton.CreateAnimation(animationName, static_cast<float>(anim->mDuration / ticksPerSecond));
			animation.SetUseBaseKeyFrame(false, 0.0f, "");
			animation.SetInterpolationMode(Animation::InterpolationMode::Linear);

			for (uint32 channelIndex = 0; channelIndex < anim->mNumChannels; ++channelIndex)
			{
				const aiNodeAnim* nodeAnim = anim->mChannels[channelIndex];

				// Try to find the given bone in our skeleton
				Bone* bone = skeleton.GetBone(nodeAnim->mNodeName.C_Str());
				if (!bone)
				{
					WLOG("Unable to find bone " << nodeAnim->mNodeName.C_Str() << " in skeleton, bone animation will not be applied!");
					continue;
				}

				const aiMatrix4x4 aiBonePoseInv = GetBindTransform(*bone).Inverse();

				// Animations are read with collapsed FBX pivots, so a channel also carries the pivot transforms
				// (pre-rotation, rotation offset, ...) that the mesh import keeps as separate parent bones named
				// "<bone>_$AssimpFbx$_<pivot>". Take them out again, or they would be applied twice.
				aiMatrix4x4 pivotsInv;
				const String pivotPrefix = String(nodeAnim->mNodeName.C_Str()) + "_$AssimpFbx$_";
				for (const Node* parent = bone->GetParent(); parent && parent->GetName().starts_with(pivotPrefix); parent = parent->GetParent())
				{
					pivotsInv = pivotsInv * GetBindTransform(*parent).Inverse();
				}

				// Create a new node track for the bone
				const uint16 handle = bone->GetHandle();
				NodeAnimationTrack* track = animation.HasNodeTrack(handle) ? animation.GetNodeTrack(handle) : animation.CreateNodeTrack(handle, bone);

				// Iterate through each key type and note the time value
				std::set<double> keyTimes;
				for (unsigned int j = 0; j < nodeAnim->mNumPositionKeys; j++)
				{
					keyTimes.insert(nodeAnim->mPositionKeys[j].mTime / ticksPerSecond);
				}

				for (unsigned int j = 0; j < nodeAnim->mNumRotationKeys; j++)
				{
					keyTimes.insert(nodeAnim->mRotationKeys[j].mTime / ticksPerSecond);
				}

				for (unsigned int j = 0; j < nodeAnim->mNumScalingKeys; j++)
				{
					keyTimes.insert(nodeAnim->mScalingKeys[j].mTime / ticksPerSecond);
				}

				const auto& boneLocalTransforms = evaluator.GetTransformations();

				// Now for each key, create a keyframe
				for (const auto time : keyTimes)
				{
					evaluator.Evaluate(time);

					// Local bone transform in the skeleton's units, without the pivot bones above it
					aiVector3D aiTrans, aiScale;
					aiQuaternion aiRot;

					boneLocalTransforms[channelIndex].Decompose(aiScale, aiRot, aiTrans);
					const aiMatrix4x4 boneLocal = pivotsInv * aiMatrix4x4(aiScale, aiRot, aiTrans * scale);
					boneLocal.Decompose(aiScale, aiRot, aiTrans);
					const Vector3 transCopy(aiTrans.x, aiTrans.y, aiTrans.z);

					const aiMatrix4x4 pose = aiBonePoseInv * boneLocal;
					pose.Decompose(aiScale, aiRot, aiTrans);

					const Quaternion rot(aiRot.w, aiRot.x, aiRot.y, aiRot.z);
					const Vector3 keyScale(aiScale.x, aiScale.y, aiScale.z);

					// This line ensures that we set the translation relative to the bind pose instead of relative to the last key frame!
					const Vector3 trans = transCopy - bone->GetPosition();

					auto keyFramePtr = track->CreateNodeKeyFrame(static_cast<float>(time));
					keyFramePtr->SetTranslate(trans);
					keyFramePtr->SetRotation(rot);
					keyFramePtr->SetScale(keyScale);
				}
			}

			animation.Optimize();
			++imported;
		}

		return imported;
	}

	bool ImportFbxAnimationsIntoAsset(const String& skeletonAsset, const std::filesystem::path& file, const FbxAnimationNameResolver& resolveName, const float scale)
	{
		const SkeletonPtr skeleton = SkeletonMgr::Get().Load(skeletonAsset);
		if (!skeleton)
		{
			ELOG("Unable to load skeleton " << skeletonAsset);
			return false;
		}

		const int32 imported = ImportFbxAnimations(*skeleton, file, resolveName, scale);
		if (imported <= 0)
		{
			ELOG("No animations imported from " << file);
			return false;
		}

		const auto filePtr = AssetRegistry::CreateNewFile(skeletonAsset);
		if (filePtr == nullptr)
		{
			ELOG("Unable to create skeleton file " << skeletonAsset);
			return false;
		}

		io::StreamSink sink{ *filePtr };
		io::Writer writer{ sink };
		SkeletonSerializer serializer;
		serializer.Export(*skeleton, writer);
		ILOG("Saved " << imported << " animations to skeleton " << skeletonAsset);
		return true;
	}
}

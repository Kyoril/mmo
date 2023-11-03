#pragma once

#include <map>

#include "base/typedefs.h"
#include "animation.h"
#include "bone.h"

#include <memory>
#include <vector>

namespace mmo
{
	struct LinkedSkeletonAnimationSource;

	enum class SkeletonAnimationBlendMode
	{
		Average,
		Cumulative
	};

	class Skeleton
	{
		friend class SkeletonInstance;

	protected:
		Skeleton() = default;

	public:
		Skeleton(const String& name);
		virtual ~Skeleton() = default;

	public:

		typedef std::vector<std::unique_ptr<Bone>> OwningBoneList;
		typedef std::vector<Bone*> BoneRefList;
		typedef std::vector<LinkedSkeletonAnimationSource> LinkedSkeletonAnimSourceList;

	public:

		const String& GetName() const { return m_name; }

		virtual Bone* CreateBone();

		virtual Bone* CreateBone(uint16 handle);

		virtual Bone* CreateBone(const String& name);

		virtual Bone* CreateBone(const String& name, uint16 handle);

		virtual uint16 GetNumBones() const;

		virtual Bone* GetRootBone() const;

		virtual Bone* GetBone(unsigned short handle) const;

		virtual Bone* GetBone(const String& name) const;

		virtual bool HasBone(const String& name) const;

		virtual void SetBindingPose();

		virtual void Reset(bool resetManualBones = false);

		virtual Animation* CreateAnimation(const String& name, const float duration);

		virtual Animation* GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const;

		virtual Animation* GetAnimation(const String& name) const;

		virtual Animation* GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker = nullptr) const;

		virtual bool HasAnimation(const String& name) const;

		virtual void RemoveAnimation(const String& name);

		virtual void GetBoneMatrices(Matrix4* matrices);

		virtual SkeletonAnimationBlendMode GetBlendMode() const { return m_blendState; }

		virtual void SetBlendMode(SkeletonAnimationBlendMode state) { m_blendState = state; }

		virtual void UpdateTransforms();

		virtual void OptimizeAllAnimations(bool preservingIdentityNodeTracks = false);

		virtual void AddLinkedSkeletonAnimationSource(const String& skelName, float scale = 1.0f);

		virtual void RemoveAllLinkedSkeletonAnimationSources();

		virtual void NotifyManualBonesDirty();

		virtual void NotifyManualBoneStateChange(Bone& bone);

		virtual bool GetManualBonesDirty() const { return m_manualBonesDirty; }

		virtual bool HasManualBones() const { return !m_manualBones.empty(); }

		/// Map to translate bone handle from one skeleton to another skeleton.
		typedef std::vector<uint16> BoneHandleMap;

		virtual void MergeSkeletonAnimations(const Skeleton* source,
			const BoneHandleMap& boneHandleMap,
			const std::vector<String>& animations = std::vector<String>());

		virtual void BuildMapBoneByHandle(const Skeleton* source, BoneHandleMap& boneHandleMap) const;

		virtual void BuildMapBoneByName(const Skeleton* source, BoneHandleMap& boneHandleMap) const;

	protected:
		void DeriveRootBone() const;

	protected:
		String m_name;

		SkeletonAnimationBlendMode m_blendState { SkeletonAnimationBlendMode::Average};

		OwningBoneList m_boneList;

		typedef std::map<String, Bone*> BoneListByName;
		BoneListByName m_boneListByName;

		mutable BoneRefList m_rootBones;

		/// Bone automatic handles
		unsigned short m_nextAutoHandle { 0 };

		typedef std::set<Bone*> BoneSet;

		BoneSet m_manualBones;

		bool m_manualBonesDirty { false };

		/// Storage of animations, lookup by name
		typedef std::map<String, Animation*> AnimationList;
		AnimationList m_animationsList;

		/// List of references to other skeletons to use animations from 
		mutable LinkedSkeletonAnimSourceList m_linkedSkeletonAnimSourceList;
	};


	typedef std::shared_ptr<Skeleton> SkeletonPtr;

	/// Link to another skeleton to share animations
	struct LinkedSkeletonAnimationSource
	{
		String skeletonName;
		SkeletonPtr skeleton;
		float scale;

		LinkedSkeletonAnimationSource(const String& skelName, float scl)
			: skeletonName(skelName)
			, scale(scl)
		{}

		LinkedSkeletonAnimationSource(const String& skelName, float scl, SkeletonPtr skelPtr)
			: skeletonName(skelName)
			, skeleton(skelPtr)
			, scale(scl) {}
	};
}

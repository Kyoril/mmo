#pragma once

#include <map>

#include "base/typedefs.h"
#include "animation.h"
#include "bone.h"

#include <memory>
#include <vector>

namespace mmo
{
	class AnimationStateSet;
}

namespace mmo
{
	struct LinkedSkeletonAnimationSource;

	enum class SkeletonAnimationBlendMode
	{
		Average,
		Cumulative,

		Count_
	};

	class Skeleton : public AnimationContainer
	{
		friend class SkeletonInstance;

	protected:
		Skeleton() = default;

	public:
		explicit Skeleton(String name);
		virtual ~Skeleton() override;

	public:

		typedef std::vector<std::unique_ptr<Bone>> OwningBoneList;
		typedef std::vector<Bone*> BoneRefList;
		typedef std::vector<LinkedSkeletonAnimationSource> LinkedSkeletonAnimSourceList;

	public:

		void Load();

		void Unload();

		virtual const String& GetName() const { return m_name; }

		/// Creates a new bone which is owned by this skeleton.
		virtual Bone* CreateBone();

		/// Creates a new bone with a specific handle which is owned by this skeleton.
		///	@param handle The handle to give the bone, must be unique within the skeleton.
		///	@returns Pointer to the new bone. Memory is owned by the skeleton.
		virtual Bone* CreateBone(uint16 handle);

		/// Creates a new bone with a specific name which is owned by this skeleton.
		///	@param name The name to give the bone, must be unique within the skeleton.
		///	@returns Pointer to the new bone. Memory is owned by the skeleton.
		virtual Bone* CreateBone(const String& name);

		/// Creates a new bone with a specific name and handle which is owned by this skeleton.
		///	@param name The name to give the bone, must be unique within the skeleton.
		///	@param handle The handle to give the bone, must be unique within the skeleton.
		///	@returns Pointer to the new bone. Memory is owned by the skeleton.
		virtual Bone* CreateBone(const String& name, uint16 handle);

		/// Gets the number of bones in this skeleton.
		///	@returns The number of bones in this skeleton.
		virtual uint16 GetNumBones() const;

		/// Gets a pointer to the root bone of the skeleton.
		///	@returns Pointer to the root bone of the skeleton. nullptr if no root bone.
		virtual Bone* GetRootBone() const;

		/// Gets a pointer to the bone with the given handle.
		///	@returns Pointer to the bone with the given handle, nullptr if not found.
		virtual Bone* GetBone(uint16 handle) const;

		/// Gets a pointer to the bone with the given name.
		///	@returns Pointer to the bone with the given name, nullptr if not found.
		virtual Bone* GetBone(const String& name) const;

		/// Gets whether a bone with the given name exists.
		///	@returns true if a bone with the given name exists, false otherwise.
		virtual bool HasBone(const String& name) const;

		/// Sets the binding pose for all bones in this skeleton to their current transformation. The binding pose
		///	marks the initial state of a bone before any animation is applied.
		virtual void SetBindingPose();

		/// Resets the transformation of all bones in this skeleton to their binding pose.
		///	@param resetManualBones If true, manual bones will be reset as well, otherwise they will be ignored.
		virtual void Reset(bool resetManualBones = false);

	public:
		// AnimationContainer implementation

		/// Creates a new animation for animating this skeleton. The animation is owned by this skeleton.
		Animation& CreateAnimation(const String& name, float duration) override;

		Animation* GetAnimation(uint16 index) const override;

		virtual Animation* GetAnimation(const String& name, const LinkedSkeletonAnimationSource** linker) const;

		Animation* GetAnimation(const String& name) const override;

		virtual Animation* GetAnimationImpl(const String& name, const LinkedSkeletonAnimationSource** linker = nullptr) const;

		bool HasAnimation(const String& name) const override;

		uint16 GetNumAnimations() const override;

		virtual void SetAnimationState(const AnimationStateSet& animSet);

		void RemoveAnimation(const String& name) override;

	public:
		virtual void InitAnimationState(AnimationStateSet& animationState);

		virtual void GetBoneMatrices(Matrix4* matrices);

		virtual SkeletonAnimationBlendMode GetBlendMode() const { return m_blendState; }

		virtual void SetBlendMode(const SkeletonAnimationBlendMode state) { m_blendState = state; }

		virtual void UpdateTransforms();

		virtual void OptimizeAllAnimations(bool preservingIdentityNodeTracks = false);

		virtual void AddLinkedSkeletonAnimationSource(const String& skeletonName, float scale = 1.0f);

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

		virtual void LoadImpl();

		virtual void UnloadImpl();

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
		typedef std::map<String, std::unique_ptr<Animation>> AnimationList;
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

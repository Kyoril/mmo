// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "skeleton_preview_provider.h"

namespace mmo
{
	SkeletonPreviewProvider::SkeletonPreviewProvider()
		: StaticTexturePreviewProvider("Editor/SkeletalMesh.htex")
	{
		// The base class constructor will handle loading the texture
	}

	const std::set<String>& SkeletonPreviewProvider::GetSupportedExtensions() const
	{
		static std::set<String> extensions { ".skel" };
		return extensions;
	}
}
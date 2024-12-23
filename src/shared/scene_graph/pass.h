// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class Pass
	{
	public:
		String m_name;
		uint32 m_hash;
		bool m_hashDirtyQueued;
		uint32 m_ambient;
		uint32 m_diffuse;
		uint32 m_specular;
		uint32 m_emissive;
		float m_shininess;
		bool m_alphaBlend;
		bool m_depthCheck;
		bool m_depthWrite;
		
	};
}
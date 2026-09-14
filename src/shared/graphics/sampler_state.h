// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/shader_base.h"

#include <memory>

namespace mmo
{
	/// @brief Texture filtering of a sampler state.
	enum class SamplerFilter
	{
		Point,
		Linear,
		Anisotropic,
		ComparisonLinear,
		ComparisonAnisotropic
	};

	/// @brief Texture coordinate addressing of a sampler state (applied to all three axes).
	enum class SamplerAddress
	{
		Clamp,
		Wrap,
		Border
	};

	/// @brief Comparison function of comparison samplers.
	enum class SamplerComparison
	{
		LessEqual,
		Less,
		Always
	};

	/// @brief Describes a sampler state.
	struct SamplerDesc
	{
		SamplerFilter filter = SamplerFilter::Linear;
		SamplerAddress address = SamplerAddress::Clamp;
		SamplerComparison comparison = SamplerComparison::LessEqual;
		float borderColor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		uint32 maxAnisotropy = 1;
	};

	/// @brief An immutable sampler state object.
	class SamplerState : public NonCopyable
	{
	public:
		~SamplerState() override = default;

	public:
		/// @brief Binds the sampler at a sampler register of a shader stage.
		virtual void Bind(ShaderType stage, uint32 slot) = 0;
	};

	using SamplerStatePtr = std::shared_ptr<SamplerState>;
}

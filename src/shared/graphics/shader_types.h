// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "../base/typedefs.h"

namespace mmo
{
    /// @brief Enumerates available pixel shader types.
    enum class PixelShaderType : uint8
    {
        /// @brief Standard forward rendering pixel shader.
        Forward,

        /// @brief Deferred rendering G-Buffer pixel shader.
        GBuffer,

        ShadowMap,
    };

    /// @brief Enumerates available vertex shader types.
    enum class VertexShaderType : uint8_t
    {
        /// @brief Default vertex shader (no skinning).
        Default,

        /// @brief Skinning profile with low amount of bones (32).
        SkinnedLow,

        /// @brief Skinning profile with medium amount of bones (64).
        SkinnedMedium,

        /// @brief Skinning profile with high amount of bones (128)
        SkinnedHigh
    };
}

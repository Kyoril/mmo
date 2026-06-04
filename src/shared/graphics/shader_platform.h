// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string_view>

namespace mmo
{
	/// @brief Well-known shader platform identifier strings used in .hmat files (format v0.5+).
	namespace shader_platform
	{
		/// Direct3D 11 shader bytecode (Windows, DXBC).
		constexpr std::string_view D3D11 = "D3D11";

		/// Metal shader bytecode (macOS / iOS).
		constexpr std::string_view Metal = "METAL";

		/// OpenGL GLSL source or SPIR-V bytecode (cross-platform).
		constexpr std::string_view OpenGL = "OPENGL";

		/// Profile string written by the legacy D3D11 compiler in pre-v0.5 .hmat files.
		/// Treated as equivalent to D3D11 by all readers.
		constexpr std::string_view LegacyD3DSM5 = "D3D_SM5";

		/// @brief Returns the platform identifier for the current compile target.
		inline constexpr std::string_view GetCurrentPlatform()
		{
#if defined(_WIN32)
			return D3D11;
#elif defined(__APPLE__)
			return Metal;
#else
			return OpenGL;
#endif
		}
	}
}

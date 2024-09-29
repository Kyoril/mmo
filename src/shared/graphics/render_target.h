// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	/// Enumerates possible clear flags.
	enum class ClearFlags
	{
		/// Nothing is cleared at all.
		None = 0,
		/// The color buffer is cleared.
		Color = 1,
		/// The depth buffer is cleared.
		Depth = 2,
		/// The stencil buffer is cleared.
		Stencil = 4,

		ColorDepth = Color | Depth,
		All = Color | Depth | Stencil,
	};


	/// 
	class RenderTarget
		: public std::enable_shared_from_this<RenderTarget>
	{
	public:
		explicit RenderTarget(std::string name, uint16 width, uint16 height) noexcept;
		virtual ~RenderTarget() noexcept = default;

	public:
		/// Activates the render target and makes all following render commands target this render target.
		virtual void Activate();
		/// Clears the render target.
		virtual void Clear(ClearFlags flags) = 0;
		/// Resizes the render target to the new size. Note that this might not happen immediately, depending on the render target.
		virtual void Resize(uint16 width, uint16 height) = 0;
		/// Updates the render target. Call after all render operations finished.
		virtual void Update() = 0;

	public:
		inline const std::string& GetName() const noexcept { return m_name; }
		inline uint16 GetWidth() const noexcept { return m_width; }
		inline uint16 GetHeight() const noexcept { return m_height; }

	protected:

		std::string m_name;
		uint16 m_width;
		uint16 m_height;
	};

	typedef std::shared_ptr<RenderTarget> RenderTargetPtr;
}
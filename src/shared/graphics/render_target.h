// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <memory>
#include <vector>

#include "texture.h"

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

	/// This class represents a multi render target, which allows binding multiple render targets of the same bit size to the pipeline at
	///	the same time, so a single pixel shader can write to multiple render targets at once. This is useful for advanced techniques
	///	such as deferred shading.
	class MultiRenderTarget
		: public RenderTarget
	{
	public:
		explicit MultiRenderTarget(std::string name, uint16 width, uint16 height, PixelFormat format) noexcept;
		~MultiRenderTarget() override = default;

	public:
		/// Adds a render target to this render target. The added render target must be the same size and bit depth and
		///	it must not be a multi render target as well, otherwise this will fail.
		///	@param renderTarget The render target to add.
		///	@return True if the render target was added, false on error.
		bool AddRenderTarget(const RenderTargetPtr& renderTarget);

		/// Removes a render target from this multi render target.
		void RemoveRenderTarget(size_t index);

		/// Gets the number of render targets in this render target.
		[[nodiscard]] size_t GetNumRenderTargets() const { return m_renderTargets.size(); }

		/// Gets the pixel format of the render target. All added render targets must use the same pixel format.
		PixelFormat GetFormat() const { return m_format; }

	public:
		virtual void Activate() override = 0;

		virtual void Resize(uint16 width, uint16 height) override = 0;

		virtual void Clear(ClearFlags flags) override = 0;

	private:
		std::vector<RenderTargetPtr> m_renderTargets;
		PixelFormat m_format;
	};

	
}

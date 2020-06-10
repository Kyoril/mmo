
#pragma once

#include "base/typedefs.h"

#include <memory>

namespace mmo
{
	/// 
	class RenderTarget
	{
	public:
		explicit RenderTarget(std::string name, uint16 width, uint16 height) noexcept;
		virtual ~RenderTarget() noexcept = default;

	public:
		/// Activates the render target and makes all following render commands target this render target.
		virtual void Activate() = 0;

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
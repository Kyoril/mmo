// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <vector>

#include "base/macros.h"
#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "configuration.h"

namespace mmo
{
	class GraphicsDevice;
	class EditorBase;
	
	class EditorApplication final : public NonCopyable
	{
	public:
		explicit EditorApplication();
		~EditorApplication() = default;

	public:
		int32 Run(int argc, char* argv[]);

	public:
		/// @brief Gets the graphics device to use for rendering.
		[[nodiscard]] GraphicsDevice& GetGraphicsDevice() const { ASSERT(m_graphicsDevice); return *m_graphicsDevice; }

		/// @brief Gets the current configuration object which contains all the relevant settings.
		[[nodiscard]] const Configuration& GetConfiguration() const noexcept { return m_configuration; }

		void AddEditor(const std::shared_ptr<EditorBase>& editor);
		
	private:
		GraphicsDevice* m_graphicsDevice { nullptr };
		Configuration m_configuration;
		std::vector<std::shared_ptr<EditorBase>> m_editors;
	};
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <filesystem>

#include "editor_host.h"
#include "base/typedefs.h"

using Path = std::filesystem::path;

namespace mmo
{
	class EditorHost;

	class EditorInstance : public NonCopyable
	{
	public:
		explicit EditorInstance(EditorHost& host, Path assetPath)
			: m_host(host)
			, m_assetPath(std::move(assetPath))
		{
		}

		virtual ~EditorInstance() override = default;

	public:
		/// @brief Draws the editor instance.
		virtual void Draw() = 0;
		
		virtual void OnMouseButtonDown(uint32 button, uint16 x, uint16 y)  {}

		virtual void OnMouseButtonUp(uint32 button, uint16 x, uint16 y)  {}

		virtual void OnMouseMoved(uint16 x, uint16 y) {}

		virtual bool Save() = 0;

	public:
		[[nodiscard]] const Path& GetAssetPath() const { return m_assetPath; }

	protected:
		EditorHost& m_host;
		Path m_assetPath;
	};
}

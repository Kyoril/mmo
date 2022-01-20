#pragma once

#include "base/non_copyable.h"

#include <filesystem>

using Path = std::filesystem::path;

namespace mmo
{
	class EditorInstance : public NonCopyable
	{
	public:
		explicit EditorInstance(Path assetPath)
			: m_assetPath(std::move(assetPath))
		{
		}

		virtual ~EditorInstance() override = default;

	public:
		/// @brief Draws the editor instance.
		virtual void Draw() = 0;

	public:
		[[nodiscard]] const Path& GetAssetPath() const noexcept { return m_assetPath; }

	protected:
		Path m_assetPath;
	};
}

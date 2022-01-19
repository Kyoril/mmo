#pragma once

#include <filesystem>

using Path = std::filesystem::path;

namespace mmo
{
	class EditorHost
	{
	public:
		virtual ~EditorHost() = default;

	public:
		virtual void SetCurrentPath(const Path& selectedPath) = 0;

		virtual const Path& GetCurrentPath() const noexcept = 0;
	};
}
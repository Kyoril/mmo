#pragma once

#include "base/non_copyable.h"

#include <memory>

namespace mmo
{
	/// @brief Base class of an editor.
	class EditorBase : public NonCopyable, public std::enable_shared_from_this<EditorBase>
	{
	public:
		EditorBase() = default;
		virtual ~EditorBase() override = default;

	public:

	};
}
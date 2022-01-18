// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <memory>

#include "base/signal.h"
#include "base/typedefs.h"

namespace mmo
{
	/// @brief Base class of an editor.
	class EditorBase : public NonCopyable, public std::enable_shared_from_this<EditorBase>
	{
	public:
		/// @brief Signal which is called whenever the editor has been modified and thus unsaved changes.
		signal<void()> modified;
		/// @brief Signal which is called whenever all unsaved changes have been saved.
		signal<void()> saved;

	public:
		/// @brief Default constructor.
		EditorBase() = default;

		/// @brief Virtual default destructor because of inheritance
		virtual ~EditorBase() override = default;

	public:
		/// @brief Determines whether the editor can load the given asset by it's extension.
		[[nodiscard]] virtual bool CanLoadAsset(const String& extension) const = 0;
	};
}

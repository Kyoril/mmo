// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "editor_instance.h"

#include "base/signal.h"
#include "base/typedefs.h"

#include <vector>
#include <memory>

namespace mmo
{
	class EditorHost;

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
		explicit EditorBase(EditorHost& host);

		/// @brief Virtual default destructor because of inheritance
		virtual ~EditorBase() override = default;

	public:
		void Draw();

	protected:
		virtual void DrawImpl() {};

	public:
		bool OpenAsset(const Path& asset);
		
		/// @brief Determines whether the editor supports asset creation.
		[[nodiscard]] virtual bool CanCreateAssets() const { return false; }

		/// @brief Determines whether the editor can load the given asset by it's extension.
		[[nodiscard]] virtual bool CanLoadAsset(const String& extension) const = 0;
		
		/// @brief Adds creation items to a context menu.
		virtual void AddCreationContextMenuItems() { };

	public:
		/// @brief Gets the editor host.
		[[nodiscard]] EditorHost& GetHost() const noexcept { return m_host; }

	protected:
		virtual std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) = 0;
		virtual void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) = 0;

	protected:
		EditorHost& m_host;
		std::vector<std::shared_ptr<EditorInstance>> m_instances;
	};
}

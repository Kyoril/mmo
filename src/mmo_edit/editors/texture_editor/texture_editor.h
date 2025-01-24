// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_base.h"

#include <map>

namespace mmo
{
	class TextureEditor : public EditorBase
	{
	public:
		TextureEditor(EditorHost& host);
		~TextureEditor() override;

	public:
		virtual void AddAssetActions(const String& asset) override;

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;

	protected:
		std::shared_ptr<EditorInstance> OpenAssetImpl(const Path& asset) override;
		void CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance) override;

	private:

		std::map<Path, std::shared_ptr<EditorInstance>> m_instances;
	};
}

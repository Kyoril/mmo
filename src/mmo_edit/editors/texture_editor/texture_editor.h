// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "editors/editor_base.h"

namespace mmo
{
	class TextureEditor : public EditorBase
	{
	public:
		TextureEditor() = default;
		~TextureEditor() override;

	public:
		[[nodiscard]] bool CanLoadAsset(const String& extension) const override;
	};
}

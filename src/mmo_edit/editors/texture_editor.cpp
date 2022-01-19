// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "texture_editor.h"

namespace mmo
{
	bool TextureEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".htex";
	}
}

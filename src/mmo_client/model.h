// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "mesh.h"
#include "sub_model.h"
#include "scene_graph/movable_object.h"

namespace mmo
{
	class Model : public MovableObject
	{
	protected:
		Model();

		Model(const String& name, const MeshPtr& mesh);

	protected:
		MeshPtr m_mesh;

		std::vector<std::unique_ptr<SubModel>> m_subEntities;


	};
}

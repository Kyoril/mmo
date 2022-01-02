// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "movable_object_factory.h"
#include "movable_object.h"

namespace mmo
{
	MovableObject* MovableObjectFactory::CreateInstance(const String& name, Scene& scene)
	{
		MovableObject* object = CreateInstanceImpl(name);
		object->SetCreator(this);
		object->SetScene(&scene);

		return object;
	}
}

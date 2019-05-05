// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "vector3.h"

namespace mmo
{
	/// Static constant zero vector.
	Vector3 Vector3::Zero;
	Vector3 Vector3::UnitX{ 1.0f, 0.0f, 0.0f };
	Vector3 Vector3::UnitY{ 0.0f, 1.0f, 0.0f };
	Vector3 Vector3::UnitZ{ 0.0f, 0.0f, 1.0f };
}

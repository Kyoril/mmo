// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "math_utils.h"
#include "matrix4.h"
#include "matrix3.h"
#include "quaternion.h"
#include "vector3.h"

#include <cmath>


namespace mmo
{
	bool FloatEqual(float a, float b, float tolerance)
	{
		return std::fabs(b - a) <= tolerance;
	}

	Matrix4 MakeViewMatrix(const Vector3& position, const Quaternion& orientation)
	{
		// View matrix is:
		//
		//  [ Lx  Uy  Dz  Tx  ]
		//  [ Lx  Uy  Dz  Ty  ]
		//  [ Lx  Uy  Dz  Tz  ]
		//  [ 0   0   0   1   ]
		//
		// Where T = -(Transposed(Rot) * Pos)

		// This is most efficiently done using 3x3 Matrices
		Matrix3 rot{};
		orientation.ToRotationMatrix(rot);

		// Make the translation relative to new axes
		const auto rotT = rot.Transpose();
		const auto trans = -rotT * position;

		// Make final matrix
		auto viewMatrix = Matrix4::Identity;
		viewMatrix = rotT;
		viewMatrix[0][3] = trans.x;
		viewMatrix[1][3] = trans.y;
		viewMatrix[2][3] = trans.z;

		return viewMatrix;
	}
}

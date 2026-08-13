// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <cmath>
#include <limits>
#include "constants.h"
#include "radian.h"


namespace mmo
{
    class Vector3;
    class Matrix4;
	class Quaternion;

	bool FloatEqual(float a, float b, float tolerance = std::numeric_limits<float>::epsilon());

    static float DegreesToRadians(const float degrees) { return degrees * Deg2Rad; }
    static float RadiansToDegrees(const float radians) { return radians * Rad2Deg; }


    Matrix4 MakeViewMatrix(const Vector3& position, const Quaternion& orientation);

	// Facing conversions
	//
	// A facing is a yaw angle around the +Y axis, and the engine's horizontal forward axis
	// is +X: a unit with facing 0 looks down +X, and increasing the facing rotates it towards
	// -Z. This matches what the client renders, which applies Quaternion(facing, UnitY) to a
	// scene node whose mesh has already been yaw-offset to point down +X.
	//
	// Always go through these helpers instead of hand-rolling the trigonometry. The sign of
	// the z component is easy to get backwards, and a mirrored conversion still looks correct
	// at facing 0, so the mistake survives casual testing.

	/// Converts a facing angle into the normalized horizontal direction the object is looking at.
	/// @param facing Yaw angle around the +Y axis.
	/// @return Unit length direction vector with a zero y component.
	Vector3 FacingToDirection(const Radian& facing);

	/// Converts a horizontal direction into the facing angle that would produce it. This is the
	/// exact inverse of FacingToDirection.
	/// @param deltaX Direction component along the x axis. Does not need to be normalized.
	/// @param deltaZ Direction component along the z axis. Does not need to be normalized.
	/// @return Facing angle in the atan2 principal range (-Pi, Pi]. Zero for a zero-length input.
	Radian DirectionToFacing(float deltaX, float deltaZ);

	/// Converts a horizontal direction into the facing angle that would produce it. The y
	/// component of the direction is ignored.
	/// @param direction Direction to look along. Does not need to be normalized.
	/// @return Facing angle in the atan2 principal range (-Pi, Pi]. Zero for a zero-length input.
	Radian DirectionToFacing(const Vector3& direction);

	/// Wraps a facing angle into the [0, 2 * Pi) range used by the movement and replication code.
	/// @param facing Facing angle to wrap.
	/// @return Equivalent facing angle in [0, 2 * Pi).
	Radian NormalizeFacingPositive(const Radian& facing);

	template<class T>
	T Interpolate(T min, T max, float t)
	{
		// Some optimization
		if (t <= 0.0f)
		{
			return min;
		}
		else if (t >= 1.0f)
		{
			return max;
		}

		// Linear interpolation
		return min + (max - min) * t;
	}

	// We store three int8_t for the X/Y/Z components.
	struct EncodedNormal8
	{
		int8_t x;
		int8_t y;
		int8_t z;
	};

	EncodedNormal8 EncodeNormalSNorm8(float nx, float ny, float nz);
	void DecodeNormalSNorm8(const EncodedNormal8& enc, float& nx, float& ny, float& nz);

	// Easing Functions
	// These functions take a parameter t in [0, 1] and return an eased value in [0, 1]
	// They are commonly used for smooth transitions in animations and interpolations

	/// EaseInOutQuad easing function
	/// Acceleration until halfway, then deceleration
	/// @param t Progress value in [0, 1]
	/// @return Eased value in [0, 1]
	float EaseInOutQuad(float t);

	/// EaseInOutCubic easing function
	/// Smoother acceleration/deceleration than quadratic
	/// @param t Progress value in [0, 1]
	/// @return Eased value in [0, 1]
	float EaseInOutCubic(float t);

	// Path Utilities

	/// Compute the angle between two path segments defined by three consecutive waypoints
	/// This is useful for determining curvature at waypoint turns
	/// @param prev Previous waypoint
	/// @param curr Current waypoint (the turn point)
	/// @param next Next waypoint
	/// @return Angle in radians, in range [0, Pi]
	float ComputeSegmentAngle(const Vector3& prev, const Vector3& curr, const Vector3& next);
}

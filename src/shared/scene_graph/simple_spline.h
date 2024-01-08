
#pragma once

#include "base/typedefs.h"
#include "math/matrix4.h"
#include "math/vector3.h"
#include <vector>

namespace mmo
{
	class SimpleSpline
	{
	public:
        SimpleSpline();

	public:
        void AddPoint(const Vector3& p);

        [[nodiscard]] const Vector3& GetPoint(uint16 index) const;

        [[nodiscard]] uint16 GetNumPoints() const;

        void Clear();

        void UpdatePoint(uint16 index, const Vector3& value);

        [[nodiscard]] Vector3 Interpolate(float t) const;

        [[nodiscard]] Vector3 Interpolate(uint32 fromIndex, float t) const;

        void SetAutoCalculate(bool autoCalc);

        void RecalculateTangents();

    protected:
        bool m_autoCalc;
        std::vector<Vector3> m_points;
        std::vector<Vector3> m_tangents;
        Matrix4 m_coefficients;
	};

    class RotationalSpline
    {
    public:
        RotationalSpline();

    public:
        void AddPoint(const Quaternion& p);

        [[nodiscard]] const Quaternion& GetPoint(uint16 index) const;

        [[nodiscard]] uint16 GetNumPoints() const;

        void Clear();

        void UpdatePoint(uint16 index, const Quaternion& value);

        Quaternion Interpolate(float t, bool useShortestPath = true);

        Quaternion Interpolate(uint32 fromIndex, float t, bool useShortestPath = true);

        void SetAutoCalculate(bool autoCalc);

        void RecalculateTangents();

    protected:

        bool m_autoCalc;
        std::vector<Quaternion> m_points;
        std::vector<Quaternion> m_tangents;

    };
}

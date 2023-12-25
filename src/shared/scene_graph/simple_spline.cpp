
#include "simple_spline.h"

namespace mmo
{
	SimpleSpline::SimpleSpline()
	{
        m_coefficients[0][0] = 2;
        m_coefficients[0][1] = -2;
        m_coefficients[0][2] = 1;
        m_coefficients[0][3] = 1;
        m_coefficients[1][0] = -3;
        m_coefficients[1][1] = 3;
        m_coefficients[1][2] = -2;
        m_coefficients[1][3] = -1;
        m_coefficients[2][0] = 0;
        m_coefficients[2][1] = 0;
        m_coefficients[2][2] = 1;
        m_coefficients[2][3] = 0;
        m_coefficients[3][0] = 1;
        m_coefficients[3][1] = 0;
        m_coefficients[3][2] = 0;
        m_coefficients[3][3] = 0;

        m_autoCalc = true;
	}

	void SimpleSpline::AddPoint(const Vector3& p)
	{
        m_points.push_back(p);
        if (m_autoCalc)
        {
            RecalculateTangents();
        }
	}

	const Vector3& SimpleSpline::GetPoint(const uint16 index) const
	{
        ASSERT(index < m_points.size() && "Point index is out of bounds!!");

        return m_points[index];
	}

	uint16 SimpleSpline::GetNumPoints() const
	{
		return static_cast<uint16>(m_points.size());
	}

	void SimpleSpline::Clear()
	{
        m_points.clear();
        m_tangents.clear();
	}

	void SimpleSpline::UpdatePoint(const uint16 index, const Vector3& value)
	{
        ASSERT(index < m_points.size() && "Point index is out of bounds!!");

        m_points[index] = value;
        if (m_autoCalc)
        {
            RecalculateTangents();
        }
	}

	Vector3 SimpleSpline::Interpolate(float t) const
	{
        float fSeg = t * (m_points.size() - 1);
        uint32 segIdx = static_cast<uint32>(fSeg);

        t = fSeg - segIdx;

        return Interpolate(segIdx, t);
	}

	Vector3 SimpleSpline::Interpolate(const uint32 fromIndex, const float t) const
	{
        ASSERT(fromIndex < m_points.size() && "fromIndex out of bounds");

        if ((fromIndex + 1) == m_points.size())
        {
            return m_points[fromIndex];
        }

        if (t == 0.0f)
        {
            return m_points[fromIndex];
        }

		if (t == 1.0f)
        {
            return m_points[fromIndex + 1];
        }

        // Real interpolation
        // Form a vector of powers of t
		const float t2 = t * t;
		const float t3 = t2 * t;
		const Vector3 powers(t3, t2, t);

        const Vector3& point1 = m_points[fromIndex];
        const Vector3& point2 = m_points[fromIndex + 1];
        const Vector3& tan1 = m_tangents[fromIndex];
        const Vector3& tan2 = m_tangents[fromIndex + 1];
        Matrix4 pt;

        pt[0][0] = point1.x;
        pt[0][1] = point1.y;
        pt[0][2] = point1.z;
        pt[0][3] = 1.0f;
        pt[1][0] = point2.x;
        pt[1][1] = point2.y;
        pt[1][2] = point2.z;
        pt[1][3] = 1.0f;
        pt[2][0] = tan1.x;
        pt[2][1] = tan1.y;
        pt[2][2] = tan1.z;
        pt[2][3] = 1.0f;
        pt[3][0] = tan2.x;
        pt[3][1] = tan2.y;
        pt[3][2] = tan2.z;
        pt[3][3] = 1.0f;

		const Vector3 ret = m_coefficients * pt * powers;
        return {ret.x, ret.y, ret.z};
	}

	void SimpleSpline::SetAutoCalculate(const bool autoCalc)
	{
        m_autoCalc = autoCalc;
	}

	void SimpleSpline::RecalculateTangents()
	{
		bool isClosed = false;

		const size_t numPoints = m_points.size();
        if (numPoints < 2)
        {
            return;
        }

        if (m_points[0] == m_points[numPoints - 1])
        {
            isClosed = true;
        }

        m_tangents.resize(numPoints);

        for (size_t i = 0; i < numPoints; ++i)
        {
            if (i == 0)
            {
                if (isClosed)
                {
                    m_tangents[i] = (m_points[1] - m_points[numPoints - 2]) * 0.5f;
                }
                else
                {
                    m_tangents[i] = (m_points[1] - m_points[0]) * 0.5f;
                }
            }
            else if (i == numPoints - 1)
            {
                if (isClosed)
                {
                    m_tangents[i] = m_tangents[0];
                }
                else
                {
                    m_tangents[i] = (m_points[i] - m_points[i - 1]) * 0.5f;
                }
            }
            else
            {
                m_tangents[i] = (m_points[i + 1] - m_points[i - 1]) * 0.5f;
            }

        }
	}

	RotationalSpline::RotationalSpline()
        : m_autoCalc(true)
	{
	}

    void RotationalSpline::AddPoint(const Quaternion& p)
    {
        m_points.push_back(p);
        if (m_autoCalc)
        {
            RecalculateTangents();
        }
    }

    const Quaternion& RotationalSpline::GetPoint(const uint16 index) const
    {
		ASSERT(index < m_points.size() && "Point index is out of bounds!!");

		return m_points[index];
    }

    uint16 RotationalSpline::GetNumPoints() const
    {
		return static_cast<unsigned short>(m_points.size());
    }

    void RotationalSpline::Clear()
    {
		m_points.clear();
		m_tangents.clear();
    }

    void RotationalSpline::UpdatePoint(const uint16 index, const Quaternion& value)
    {
        ASSERT(index < m_points.size() && "Point index is out of bounds!!");

        m_points[index] = value;
        if (m_autoCalc)
        {
            RecalculateTangents();
        }
    }

    Quaternion RotationalSpline::Interpolate(float t, const bool useShortestPath)
    {
	    const float fSeg = t * (m_points.size() - 1);
	    const unsigned int segIdx = static_cast<unsigned int>(fSeg);
        t = fSeg - segIdx;

        return Interpolate(segIdx, t, useShortestPath);
    }

    Quaternion RotationalSpline::Interpolate(const uint32 fromIndex, const float t, const bool useShortestPath)
    {
        ASSERT(fromIndex < m_points.size() && "fromIndex out of bounds");

        if ((fromIndex + 1) == m_points.size())
        {
            return m_points[fromIndex];
        }

        if (t == 0.0f)
        {
            return m_points[fromIndex];
        }

    	if (t == 1.0f)
        {
            return m_points[fromIndex + 1];
        }

        const Quaternion& p = m_points[fromIndex];
        const Quaternion& q = m_points[fromIndex + 1];
        const Quaternion& a = m_tangents[fromIndex];
        const Quaternion& b = m_tangents[fromIndex + 1];

        return Quaternion::Squad(t, p, a, b, q, useShortestPath);
    }

    void RotationalSpline::SetAutoCalculate(const bool autoCalc)
    {
		m_autoCalc = autoCalc;
    }

    void RotationalSpline::RecalculateTangents()
    {
	    bool isClosed = false;

	    const uint32 numPoints = static_cast<uint32>(m_points.size());

        if (numPoints < 2)
        {
            // Can't do anything yet
            return;
        }

        m_tangents.resize(numPoints);

        if (m_points[0] == m_points[numPoints - 1])
        {
            isClosed = true;
        }

        Quaternion part1, part2;
        for (size_t i = 0; i < numPoints; ++i)
        {
            Quaternion& p = m_points[i];
            Quaternion invp = p.Inverse();

            if (i == 0)
            {
                // special case start
                part1 = (invp * m_points[i + 1]).Log();
                if (isClosed)
                {
                    // Use numPoints-2 since numPoints-1 == end == start == this one
                    part2 = (invp * m_points[numPoints - 2]).Log();
                }
                else
                {
                    part2 = (invp * p).Log();
                }
            }
            else if (i == numPoints - 1)
            {
                // special case end
                if (isClosed)
                {
                    // Wrap to [1] (not [0], this is the same as end == this one)
                    part1 = (invp * m_points[1]).Log();
                }
                else
                {
                    part1 = (invp * p).Log();
                }
                part2 = (invp * m_points[i - 1]).Log();
            }
            else
            {
                part1 = (invp * m_points[i + 1]).Log();
                part2 = (invp * m_points[i - 1]).Log();
            }

            Quaternion preExp = -0.25f * (part1 + part2);
            m_tangents[i] = p * preExp.Exp();
        }
    }
}

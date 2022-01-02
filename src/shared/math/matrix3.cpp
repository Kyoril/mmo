// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "matrix3.h"
#include "vector3.h"

#include "base/macros.h"


namespace mmo
{
    const float Matrix3::Epsilon = 1e-06f;
    const Matrix3 Matrix3::Zero(0, 0, 0, 0, 0, 0, 0, 0, 0);
    const Matrix3 Matrix3::Identity(1, 0, 0, 0, 1, 0, 0, 0, 1);
    const float Matrix3::msSvdEpsilon = 1e-04f;
    const unsigned int Matrix3::msSvdMaxIterations = 32;

    bool Matrix3::operator==(const Matrix3& r) const
    {
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                if (m[row][col] != r.m[row][col])
                    return false;
            }
        }

        return true;
    }

    Matrix3 Matrix3::operator+(const Matrix3& r) const
    {
        Matrix3 sum{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                sum.m[row][col] = m[row][col] + r.m[row][col];
            }
        }
    	
        return sum;
    }

    Matrix3 Matrix3::operator-(const Matrix3& r) const
    {
        Matrix3 diff{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                diff.m[row][col] = m[row][col] - r.m[row][col];
            }
        }
    	
        return diff;
    }

    Matrix3 Matrix3::operator*(const Matrix3& r) const
    {
        Matrix3 prod{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                prod.m[row][col] =
                    m[row][0] * r.m[0][col] +
                    m[row][1] * r.m[1][col] +
                    m[row][2] * r.m[2][col];
            }
        }
    	
        return prod;
    }

    Matrix3 Matrix3::operator-() const
    {
        Matrix3 neg{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                neg[row][col] = -m[row][col];
            }
        }
    	
        return neg;
    }

    Vector3 Matrix3::operator*(const Vector3& r) const
    {
        Vector3 prod{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            prod[row] =
                m[row][0] * r[0] +
                m[row][1] * r[1] +
                m[row][2] * r[2];
        }
    	
        return prod;
    }

    Matrix3 Matrix3::operator*(float scalar) const
    {
        Matrix3 prod{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                prod[row][col] = scalar * m[row][col];
            }
        }
    	
        return prod;
    }

    Vector3 Matrix3::GetColumn(size_t col) const
    {
        ASSERT(col < 3);
        return Vector3(m[0][col], m[1][col], m[2][col]);
    }

    void Matrix3::SetColumn(size_t col, const Vector3& vec)
    {
        ASSERT(col < 3);
        m[0][col] = vec.x;
        m[1][col] = vec.y;
        m[2][col] = vec.z;
    }

    void Matrix3::FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
    {
        SetColumn(0, xAxis);
        SetColumn(1, yAxis);
        SetColumn(2, zAxis);
    }

    Matrix3 Matrix3::Transpose() const
    {
        Matrix3 transpose{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                transpose[row][col] = m[col][row];
            }
        }
    	
        return transpose;
    }

    bool Matrix3::Inverse(Matrix3& inverse, float tolerance) const
    {
        inverse[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
        inverse[0][1] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
        inverse[0][2] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
        inverse[1][0] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
        inverse[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
        inverse[1][2] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
        inverse[2][0] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
        inverse[2][1] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
        inverse[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];

        const auto determinant =
            m[0][0] * inverse[0][0] +
            m[0][1] * inverse[1][0] +
            m[0][2] * inverse[2][0];

        if (fabsf(determinant) <= tolerance)
            return false;

        const auto inverseDeterminant = 1.0f / determinant;
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                inverse[row][col] *= inverseDeterminant;
            }
        }

        return true;
    }

    Matrix3 Matrix3::Inverse(float fTolerance) const
    {
        Matrix3 inverse = Matrix3::Zero;
    	
        Inverse(inverse, fTolerance);
    	
        return inverse;
    }

    float Matrix3::Determinant() const
    {
        const auto coFactor00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
        const auto coFactor10 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
        const auto coFactor20 = m[1][0] * m[2][1] - m[1][1] * m[2][0];

        const auto determinant =
            m[0][0] * coFactor00 +
            m[0][1] * coFactor10 +
            m[0][2] * coFactor20;

        return determinant;
    }

    void Matrix3::SingularValueDecomposition(Matrix3& l, Vector3& s, Matrix3& r) const
    {
        size_t row, col;

        auto kA = *this;
        BiDiagonalize(kA, l, r);

        for (auto i = 0; i < msSvdMaxIterations; i++)
        {
            float fTmp, fTmp0, fTmp1;
            float fSin0, fCos0, fTan0;
            float fSin1, fCos1, fTan1;

            const auto bTest1 = 
                (::fabsf(kA[0][1]) <= msSvdEpsilon * (::fabsf(kA[0][0]) + ::fabsf(kA[1][1])));
            const auto bTest2 = 
                (::fabsf(kA[1][2]) <= msSvdEpsilon * (::fabsf(kA[1][1]) + ::fabsf(kA[2][2])));
            if (bTest1)
            {
                if (bTest2)
                {
                    s[0] = kA[0][0];
                    s[1] = kA[1][1];
                    s[2] = kA[2][2];
                    break;
                }

                fTmp = (kA[1][1] * kA[1][1] - kA[2][2] * kA[2][2] +
                    kA[1][2] * kA[1][2]) / (kA[1][2] * kA[2][2]);
                fTan0 = 0.5f * (fTmp + ::sqrtf(fTmp * fTmp + 4.0f));
                fCos0 = 1.0f / ::sqrtf(1.0f + fTan0 * fTan0);
                fSin0 = fTan0 * fCos0;

                for (col = 0; col < 3; col++)
                {
                    fTmp0 = l[col][1];
                    fTmp1 = l[col][2];
                    l[col][1] = fCos0 * fTmp0 - fSin0 * fTmp1;
                    l[col][2] = fSin0 * fTmp0 + fCos0 * fTmp1;
                }

                fTan1 = (kA[1][2] - kA[2][2] * fTan0) / kA[1][1];
                fCos1 = ::sqrtf(1.0f + fTan1 * fTan1);
                fSin1 = -fTan1 * fCos1;

                for (row = 0; row < 3; row++)
                {
                    fTmp0 = r[1][row];
                    fTmp1 = r[2][row];
                    r[1][row] = fCos1 * fTmp0 - fSin1 * fTmp1;
                    r[2][row] = fSin1 * fTmp0 + fCos1 * fTmp1;
                }

                s[0] = kA[0][0];
                s[1] = fCos0 * fCos1 * kA[1][1] -
                    fSin1 * (fCos0 * kA[1][2] - fSin0 * kA[2][2]);
                s[2] = fSin0 * fSin1 * kA[1][1] +
                    fCos1 * (fSin0 * kA[1][2] + fCos0 * kA[2][2]);
                break;
            }
        	
            if (bTest2)
            {
                fTmp = (kA[0][0] * kA[0][0] + kA[1][1] * kA[1][1] -
                    kA[0][1] * kA[0][1]) / (kA[0][1] * kA[1][1]);
                fTan0 = 0.5f * (-fTmp + ::sqrtf(fTmp * fTmp + 4.0f));
                fCos0 = 1.0f / ::sqrtf(1.0f + fTan0 * fTan0);
                fSin0 = fTan0 * fCos0;

                for (col = 0; col < 3; col++)
                {
                    fTmp0 = l[col][0];
                    fTmp1 = l[col][1];
                    l[col][0] = fCos0 * fTmp0 - fSin0 * fTmp1;
                    l[col][1] = fSin0 * fTmp0 + fCos0 * fTmp1;
                }

                fTan1 = (kA[0][1] - kA[1][1] * fTan0) / kA[0][0];
                fCos1 = 1.0f / ::sqrtf(1.0f + fTan1 * fTan1);
                fSin1 = -fTan1 * fCos1;

                for (row = 0; row < 3; row++)
                {
                    fTmp0 = r[0][row];
                    fTmp1 = r[1][row];
                    r[0][row] = fCos1 * fTmp0 - fSin1 * fTmp1;
                    r[1][row] = fSin1 * fTmp0 + fCos1 * fTmp1;
                }

                s[0] = fCos0 * fCos1 * kA[0][0] -
                    fSin1 * (fCos0 * kA[0][1] - fSin0 * kA[1][1]);
                s[1] = fSin0 * fSin1 * kA[0][0] +
                    fCos1 * (fSin0 * kA[0][1] + fCos0 * kA[1][1]);
                s[2] = kA[2][2];
                break;
            }

            GolubKahanStep(kA, l, r);
        }

        for (row = 0; row < 3; row++)
        {
            if (s[row] < 0.0f)
            {
                s[row] = -s[row];
                for (col = 0; col < 3; col++)
                {
                    r[row][col] = -r[row][col];
                }
            }
        }
    }

    void Matrix3::SingularValueComposition(const Matrix3& l, const Vector3& s, const Matrix3& r)
    {
        size_t row, col;
        Matrix3 tmp{};

        for (row = 0; row < 3; row++)
        {
            for (col = 0; col < 3; col++)
            {
                tmp[row][col] = s[row] * r[row][col];
            }
        }

        for (row = 0; row < 3; row++)
        {
            for (col = 0; col < 3; col++)
            {
                m[row][col] = 0.0;
                for (auto mid = 0; mid < 3; mid++)
                {
                    m[row][col] += l[row][mid] * tmp[mid][col];
                }
            }
        }
    }

    void Matrix3::Orthonormalize()
    {
    	// compute q0
        auto invLength = 1.0f / ::sqrtf(m[0][0] * m[0][0]
            + m[1][0] * m[1][0] + m[2][0] * m[2][0]);

        m[0][0] *= invLength;
        m[1][0] *= invLength;
        m[2][0] *= invLength;

        auto dot0 =
            m[0][0] * m[0][1] +
            m[1][0] * m[1][1] +
            m[2][0] * m[2][1];

        m[0][1] -= dot0 * m[0][0];
        m[1][1] -= dot0 * m[1][0];
        m[2][1] -= dot0 * m[2][0];

        invLength = 1.0f / ::sqrtf(m[0][1] * m[0][1] +
            m[1][1] * m[1][1] +
            m[2][1] * m[2][1]);

        m[0][1] *= invLength;
        m[1][1] *= invLength;
        m[2][1] *= invLength;

        const auto dot1 =
            m[0][1] * m[0][2] +
            m[1][1] * m[1][2] +
            m[2][1] * m[2][2];

        dot0 =
            m[0][0] * m[0][2] +
            m[1][0] * m[1][2] +
            m[2][0] * m[2][2];

        m[0][2] -= dot0 * m[0][0] + dot1 * m[0][1];
        m[1][2] -= dot0 * m[1][0] + dot1 * m[1][1];
        m[2][2] -= dot0 * m[2][0] + dot1 * m[2][1];

        invLength = 1.0f / ::sqrtf(m[0][2] * m[0][2] +
            m[1][2] * m[1][2] +
            m[2][2] * m[2][2]);

        m[0][2] *= invLength;
        m[1][2] *= invLength;
        m[2][2] *= invLength;
    }

    void Matrix3::QDUDecomposition(Matrix3& q, Vector3& d, Vector3& u) const
    {
        auto invLength = 1.0f / ::sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
        q[0][0] = m[0][0] * invLength;
        q[1][0] = m[1][0] * invLength;
        q[2][0] = m[2][0] * invLength;

        auto dot = q[0][0] * m[0][1] + q[1][0] * m[1][1] + q[2][0] * m[2][1];
        q[0][1] = m[0][1] - dot * q[0][0];
        q[1][1] = m[1][1] - dot * q[1][0];
        q[2][1] = m[2][1] - dot * q[2][0];
        invLength = 1.0f / ::sqrtf(q[0][1] * q[0][1] + q[1][1] * q[1][1] + q[2][1] * q[2][1]);

        q[0][1] *= invLength;
        q[1][1] *= invLength;
        q[2][1] *= invLength;

        dot = q[0][0] * m[0][2] + q[1][0] * m[1][2] + q[2][0] * m[2][2];
        q[0][2] = m[0][2] - dot * q[0][0];
        q[1][2] = m[1][2] - dot * q[1][0];
        q[2][2] = m[2][2] - dot * q[2][0];
    	
        dot = q[0][1] * m[0][2] + q[1][1] * m[1][2] + q[2][1] * m[2][2];
        q[0][2] -= dot * q[0][1];
        q[1][2] -= dot * q[1][1];
        q[2][2] -= dot * q[2][1];
    	
        invLength = 1.0f / ::sqrtf(q[0][2] * q[0][2] + q[1][2] * q[1][2] + q[2][2] * q[2][2]);

        q[0][2] *= invLength;
        q[1][2] *= invLength;
        q[2][2] *= invLength;

        const auto fDet = q[0][0] * q[1][1] * q[2][2] + q[0][1] * q[1][2] * q[2][0] +
            q[0][2] * q[1][0] * q[2][1] - q[0][2] * q[1][1] * q[2][0] -
            q[0][1] * q[1][0] * q[2][2] - q[0][0] * q[1][2] * q[2][1];

        if (fDet < 0.0f)
        {
            for (size_t row = 0; row < 3; row++)
            {
                for (size_t col = 0; col < 3; col++)
                {
                    q[row][col] = -q[row][col];
                }
            }
        }

        Matrix3 kR{};
        kR[0][0] = q[0][0] * m[0][0] + q[1][0] * m[1][0] + q[2][0] * m[2][0];
        kR[0][1] = q[0][0] * m[0][1] + q[1][0] * m[1][1] + q[2][0] * m[2][1];
        kR[1][1] = q[0][1] * m[0][1] + q[1][1] * m[1][1] + q[2][1] * m[2][1];
        kR[0][2] = q[0][0] * m[0][2] + q[1][0] * m[1][2] + q[2][0] * m[2][2];
        kR[1][2] = q[0][1] * m[0][2] + q[1][1] * m[1][2] + q[2][1] * m[2][2];
        kR[2][2] = q[0][2] * m[0][2] + q[1][2] * m[1][2] + q[2][2] * m[2][2];

        d[0] = kR[0][0];
        d[1] = kR[1][1];
        d[2] = kR[2][2];

        const auto fInvD0 = 1.0f / d[0];
        u[0] = kR[0][1] * fInvD0;
        u[1] = kR[0][2] * fInvD0;
        u[2] = kR[1][2] / d[1];
    }

    float Matrix3::SpectralNorm() const
    {
        Matrix3 kP{};
    	
        size_t row, col;
        float max = 0.0;
    	
        for (row = 0; row < 3; row++)
        {
            for (col = 0; col < 3; col++)
            {
                kP[row][col] = 0.0;
                for (auto mid = 0; mid < 3; mid++)
                {
                    kP[row][col] += m[mid][row] * m[mid][col];
                }
            	
                if (kP[row][col] > max) max = kP[row][col];
            }
        }

        const auto invMax = 1.0f / max;
        for (row = 0; row < 3; row++)
        {
            for (col = 0; col < 3; col++)
            {
                kP[row][col] *= invMax;
            }
        }

        float coefficients[3];
        coefficients[0] = -(kP[0][0] * (kP[1][1] * kP[2][2] - kP[1][2] * kP[2][1]) +
            kP[0][1] * (kP[2][0] * kP[1][2] - kP[1][0] * kP[2][2]) +
            kP[0][2] * (kP[1][0] * kP[2][1] - kP[2][0] * kP[1][1]));
        coefficients[1] = kP[0][0] * kP[1][1] - kP[0][1] * kP[1][0] +
            kP[0][0] * kP[2][2] - kP[0][2] * kP[2][0] +
            kP[1][1] * kP[2][2] - kP[1][2] * kP[2][1];
        coefficients[2] = -(kP[0][0] + kP[1][1] + kP[2][2]);

        const auto fRoot = MaxCubicRoot(coefficients);
        return ::sqrtf(max * fRoot);
    }

    void Matrix3::ToAngleAxis(Vector3& axis, Radian& angle) const
    {
        const auto fTrace = m[0][0] + m[1][1] + m[2][2];
        const auto fCos = 0.5f * (fTrace - 1.0f);
        angle = ::acosf(fCos);

        if (angle > Radian(0.0))
        {
            if (angle < Radian(Pi))
            {
                axis.x = m[2][1] - m[1][2];
                axis.y = m[0][2] - m[2][0];
                axis.z = m[1][0] - m[0][1];
                axis.Normalize();
            }
            else
            {
                float fHalfInverse;
                if (m[0][0] >= m[1][1])
                {
                    if (m[0][0] >= m[2][2])
                    {
                        axis.x = 0.5f * ::sqrtf(m[0][0] -
                            m[1][1] - m[2][2] + 1.0f);
                        fHalfInverse = 0.5f / axis.x;
                        axis.y = fHalfInverse * m[0][1];
                        axis.z = fHalfInverse * m[0][2];
                    }
                    else
                    {
                        axis.z = 0.5f * ::sqrtf(m[2][2] -
                            m[0][0] - m[1][1] + 1.0f);
                        fHalfInverse = 0.5f / axis.z;
                        axis.x = fHalfInverse * m[0][2];
                        axis.y = fHalfInverse * m[1][2];
                    }
                }
                else
                {
                    if (m[1][1] >= m[2][2])
                    {
                        axis.y = 0.5f * ::sqrtf(m[1][1] -
                            m[0][0] - m[2][2] + 1.0f);
                        fHalfInverse = 0.5f / axis.y;
                        axis.x = fHalfInverse * m[0][1];
                        axis.z = fHalfInverse * m[1][2];
                    }
                    else
                    {
                        axis.z = 0.5f * ::sqrtf(m[2][2] -
                            m[0][0] - m[1][1] + 1.0f);
                        fHalfInverse = 0.5f / axis.z;
                        axis.x = fHalfInverse * m[0][2];
                        axis.y = fHalfInverse * m[1][2];
                    }
                }
            }
        }
        else
        {
            axis.x = 1.0;
            axis.y = 0.0;
            axis.z = 0.0;
        }
    }

    void Matrix3::FromAngleAxis(const Vector3& axis, const Radian& angle)
    {
        const auto fCos = ::cosf(angle.GetValueRadians());
        const auto fSin = ::sinf(angle.GetValueRadians());
        const auto fOneMinusCos = 1.0f - fCos;
        const auto fX2 = axis.x * axis.x;
        const auto fY2 = axis.y * axis.y;
        const auto fZ2 = axis.z * axis.z;
        const auto fXYM = axis.x * axis.y * fOneMinusCos;
        const auto fXZM = axis.x * axis.z * fOneMinusCos;
        const auto fYZM = axis.y * axis.z * fOneMinusCos;
        const auto fXSin = axis.x * fSin;
        const auto fYSin = axis.y * fSin;
        const auto fZSin = axis.z * fSin;

        m[0][0] = fX2 * fOneMinusCos + fCos;
        m[0][1] = fXYM - fZSin;
        m[0][2] = fXZM + fYSin;
        m[1][0] = fXYM + fZSin;
        m[1][1] = fY2 * fOneMinusCos + fCos;
        m[1][2] = fYZM - fXSin;
        m[2][0] = fXZM - fYSin;
        m[2][1] = fYZM + fXSin;
        m[2][2] = fZ2 * fOneMinusCos + fCos;
    }

    bool Matrix3::ToEulerAnglesXYZ(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = Radian(::asinf(m[0][2]));
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(-m[1][2], m[2][2]);
                rAngle = ::atan2f(-m[0][1], m[0][0]);
                return true;
            }
        	
            const auto fRmY = Radian(::atan2f(m[1][0], m[1][1]));
            rAngle = Radian(0.0f);  // any angle works
            yAngle = rAngle - fRmY;
            return false;
        }

        const auto fRpY = Radian(::atan2f(m[1][0], m[1][1]));
        rAngle = Radian(0.0f);  // any angle works
        yAngle = fRpY - rAngle;
        return false;
    }

    bool Matrix3::ToEulerAnglesXZY(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = ::asinf(-m[0][1]);
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(m[2][1], m[1][1]);
                rAngle = ::atan2f(m[0][2], m[0][0]);
                return true;
            }

            const auto fRmY = Radian(::atan2f(-m[2][0], m[2][2]));
            rAngle = Radian(0.0f);
            yAngle = rAngle - fRmY;
            return false;
        }

        const auto fRpY = Radian(::atan2f(-m[2][0], m[2][2]));
        rAngle = Radian(0.0f);
        yAngle = fRpY - rAngle;
        return false;
    }

    bool Matrix3::ToEulerAnglesYXZ(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = ::asinf(-m[1][2]);
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(m[0][2], m[2][2]);
                rAngle = ::atan2f(m[1][0], m[1][1]);
                return true;
            }

            const auto fRmY = Radian(::atan2f(-m[0][1], m[0][0]));
            rAngle = Radian(0.0f);
            yAngle = rAngle - fRmY;
            return false;
        }

        const auto fRpY = Radian(::atan2f(-m[0][1], m[0][0]));
        rAngle = Radian(0.0f);
        yAngle = fRpY - rAngle;
        return false;
    }

    bool Matrix3::ToEulerAnglesYZX(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = ::asinf(m[1][0]);
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(-m[2][0], m[0][0]);
                rAngle = ::atan2f(-m[1][2], m[1][1]);
                return true;
            }

            const auto fRmY = Radian(::atan2f(m[2][1], m[2][2]));
            rAngle = Radian(0.0f);  // any angle works
            yAngle = rAngle - fRmY;
            return false;
        }

    	const auto fRpY = Radian(::atan2f(m[2][1], m[2][2]));
        rAngle = Radian(0.0f);
        yAngle = fRpY - rAngle;
        return false;
    }

    bool Matrix3::ToEulerAnglesZXY(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = ::asinf(m[2][1]);
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(-m[0][1], m[1][1]);
                rAngle = ::atan2f(-m[2][0], m[2][2]);
                return true;
            }
        	
            const auto fRmY = Radian(::atan2f(m[0][2], m[0][0]));
            rAngle = Radian(0.0f);  // any angle works
            yAngle = rAngle - fRmY;
            return false;
        }

        const auto fRpY = Radian(::atan2f(m[0][2], m[0][0]));
        rAngle = Radian(0.0f);  // any angle works
        yAngle = fRpY - rAngle;
        return false;
    }

    bool Matrix3::ToEulerAnglesZYX(Radian& yAngle, Radian& pAngle, Radian& rAngle) const
    {
        pAngle = ::asinf(-m[2][0]);
        if (pAngle < Radian(HalfPi))
        {
            if (pAngle > Radian(-HalfPi))
            {
                yAngle = ::atan2f(m[1][0], m[0][0]);
                rAngle = ::atan2f(m[2][1], m[2][2]);
                return true;
            }

            const auto fRmY = Radian(::atan2f(-m[0][1], m[0][2]));
            rAngle = Radian(0.0f);
            yAngle = rAngle - fRmY;
            return false;
        }
    	
        const auto fRpY = Radian(::atan2f(-m[0][1], m[0][2]));
        rAngle = Radian(0.0f);
        yAngle = fRpY - rAngle;
        return false;
    }

    void Matrix3::FromEulerAnglesXYZ(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
        auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        *this = kXMat * (kYMat * kZMat);
    }

    void Matrix3::FromEulerAnglesXZY(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
        auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        *this = kXMat * (kZMat * kYMat);
    }

    void Matrix3::FromEulerAnglesYXZ(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
        auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        *this = kYMat * (kXMat * kZMat);
    }

    void Matrix3::FromEulerAnglesYZX(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
        auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        *this = kYMat * (kZMat * kXMat);
    }

    void Matrix3::FromEulerAnglesZXY(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
        auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        *this = kZMat * (kXMat * kYMat);
    }

    void Matrix3::FromEulerAnglesZYX(const Radian& yAngle, const Radian& pAngle, const Radian& rAngle)
    {
	    auto fCos = ::cosf(yAngle.GetValueRadians());
        auto fSin = ::sinf(yAngle.GetValueRadians());
        const Matrix3 kZMat(fCos, -fSin, 0.0f, fSin, fCos, 0.0f, 0.0f, 0.0f, 1.0f);

        fCos = ::cosf(pAngle.GetValueRadians());
        fSin = ::sinf(pAngle.GetValueRadians());
        const Matrix3 kYMat(fCos, 0.0f, fSin, 0.0f, 1.0f, 0.0f, -fSin, 0.0f, fCos);

        fCos = ::cosf(rAngle.GetValueRadians());
        fSin = ::sinf(rAngle.GetValueRadians());
        const Matrix3 kXMat(1.0f, 0.0f, 0.0f, 0.0f, fCos, -fSin, 0.0f, fSin, fCos);

        *this = kZMat * (kYMat * kXMat);
    }

    void Matrix3::EigenSolveSymmetric(float eigenValue[3], Vector3 eigenVector[3]) const
    {
	    auto kMatrix = *this;
    	
        float subDiagonals[3];
        kMatrix.Tridiagonal(eigenValue, subDiagonals);
        kMatrix.QLAlgorithm(eigenValue, subDiagonals);

        for (size_t i = 0; i < 3; i++)
        {
            eigenVector[i][0] = kMatrix[0][i];
            eigenVector[i][1] = kMatrix[1][i];
            eigenVector[i][2] = kMatrix[2][i];
        }

        const auto cross = eigenVector[1].Cross(eigenVector[2]);
        const auto det = eigenVector[0].Dot(cross);
        if (det < 0.0f)
        {
            eigenVector[2][0] = -eigenVector[2][0];
            eigenVector[2][1] = -eigenVector[2][1];
            eigenVector[2][2] = -eigenVector[2][2];
        }
    }

    void Matrix3::TensorProduct(const Vector3& u, const Vector3& v, Matrix3& product)
    {
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                product[row][col] = u[row] * v[col];
            }
        }
    }

    void Matrix3::Tridiagonal(float diagonals[3], float subDiagonals[3])
    {
        const auto fA = m[0][0];
        auto fB = m[0][1];
        auto fC = m[0][2];
        const auto fD = m[1][1];
        const auto fE = m[1][2];
        const auto fF = m[2][2];

        diagonals[0] = fA;
        subDiagonals[2] = 0.0;
        if (::fabsf(fC) >= Epsilon)
        {
            const auto length = ::sqrtf(fB * fB + fC * fC);
            const auto invLength = 1.0f / length;
            fB *= invLength;
            fC *= invLength;
            const auto fQ = 2.0f * fB * fE + fC * (fF - fD);
            diagonals[1] = fD + fC * fQ;
            diagonals[2] = fF - fC * fQ;
            subDiagonals[0] = length;
            subDiagonals[1] = fE - fB * fQ;
            m[0][0] = 1.0;
            m[0][1] = 0.0;
            m[0][2] = 0.0;
            m[1][0] = 0.0;
            m[1][1] = fB;
            m[1][2] = fC;
            m[2][0] = 0.0;
            m[2][1] = fC;
            m[2][2] = -fB;
        }
        else
        {
            diagonals[1] = fD;
            diagonals[2] = fF;
            subDiagonals[0] = fB;
            subDiagonals[1] = fE;
            m[0][0] = 1.0;
            m[0][1] = 0.0;
            m[0][2] = 0.0;
            m[1][0] = 0.0;
            m[1][1] = 1.0;
            m[1][2] = 0.0;
            m[2][0] = 0.0;
            m[2][1] = 0.0;
            m[2][2] = 1.0;
        }
    }

    bool Matrix3::QLAlgorithm(float diagonals[3], float subDiagonals[3])
    {
        for (auto i0 = 0; i0 < 3; i0++)
        {
            const auto iMaxIter = 32;
            unsigned int iIter;
        	
            for (iIter = 0; iIter < iMaxIter; iIter++)
            {
                int i1;
                for (i1 = i0; i1 <= 1; i1++)
                {
                    const auto fSum = ::fabsf(diagonals[i1]) +
                        ::fabsf(diagonals[i1 + 1]);
                	
                    if ((::fabsf(subDiagonals[i1]) + fSum) == fSum)
                        break;
                }
            	
                if (i1 == i0) break;

                auto fTmp0 = (diagonals[i0 + 1] - diagonals[i0]) / (2.0f * subDiagonals[i0]);
                auto fTmp1 = ::sqrtf(fTmp0 * fTmp0 + 1.0f);
            	
                if (fTmp0 < 0.0f)
                {
                    fTmp0 = diagonals[i1] - diagonals[i0] + subDiagonals[i0] / (fTmp0 - fTmp1);
                }
                else
                {
                    fTmp0 = diagonals[i1] - diagonals[i0] + subDiagonals[i0] / (fTmp0 + fTmp1);
                }
            	
                auto fSin = 1.0f;
                auto fCos = 1.0f;
                auto fTmp2 = 0.0f;
            	
                for (auto i2 = i1 - 1; i2 >= i0; i2--)
                {
                    auto fTmp3 = fSin * subDiagonals[i2];
                    const auto fTmp4 = fCos * subDiagonals[i2];
                    if (::fabsf(fTmp3) >= ::fabsf(fTmp0))
                    {
                        fCos = fTmp0 / fTmp3;
                        fTmp1 = ::sqrtf(fCos * fCos + 1.0f);
                        subDiagonals[i2 + 1] = fTmp3 * fTmp1;
                        fSin = 1.0f / fTmp1;
                        fCos *= fSin;
                    }
                    else
                    {
                        fSin = fTmp3 / fTmp0;
                        fTmp1 = ::sqrtf(fSin * fSin + 1.0f);
                        subDiagonals[i2 + 1] = fTmp0 * fTmp1;
                        fCos = 1.0f / fTmp1;
                        fSin *= fCos;
                    }
                	
                    fTmp0 = diagonals[i2 + 1] - fTmp2;
                    fTmp1 = (diagonals[i2] - fTmp0) * fSin + 2.0f * fTmp4 * fCos;
                    fTmp2 = fSin * fTmp1;
                    diagonals[i2 + 1] = fTmp0 + fTmp2;
                    fTmp0 = fCos * fTmp1 - fTmp4;

                    for (auto row = 0; row < 3; row++)
                    {
                        fTmp3 = m[row][i2 + 1];
                        m[row][i2 + 1] = fSin * m[row][i2] + fCos * fTmp3;
                        m[row][i2] = fCos * m[row][i2] - fSin * fTmp3;
                    }
                }
                diagonals[i0] -= fTmp2;
                subDiagonals[i0] = fTmp0;
                subDiagonals[i1] = 0.0;
            }

            if (iIter == iMaxIter)
            {
                return false;
            }
        }

        return true;
    }

    void Matrix3::BiDiagonalize(Matrix3& kA, Matrix3& kL, Matrix3& kR)
    {
        float afV[3], afW[3];
        float fSign, fT1, fT2;
        bool identity;

        auto length = ::sqrtf(kA[0][0] * kA[0][0] + kA[1][0] * kA[1][0] + kA[2][0] * kA[2][0]);
        if (length > 0.0f)
        {
            fSign = (kA[0][0] > 0.0f ? 1.0f : -1.0f);
            fT1 = kA[0][0] + fSign * length;
            const auto fInvT1 = 1.0f / fT1;
            afV[1] = kA[1][0] * fInvT1;
            afV[2] = kA[2][0] * fInvT1;

            fT2 = -2.0f / (1.0f + afV[1] * afV[1] + afV[2] * afV[2]);
            afW[0] = fT2 * (kA[0][0] + kA[1][0] * afV[1] + kA[2][0] * afV[2]);
            afW[1] = fT2 * (kA[0][1] + kA[1][1] * afV[1] + kA[2][1] * afV[2]);
            afW[2] = fT2 * (kA[0][2] + kA[1][2] * afV[1] + kA[2][2] * afV[2]);
            kA[0][0] += afW[0];
            kA[0][1] += afW[1];
            kA[0][2] += afW[2];
            kA[1][1] += afV[1] * afW[1];
            kA[1][2] += afV[1] * afW[2];
            kA[2][1] += afV[2] * afW[1];
            kA[2][2] += afV[2] * afW[2];

            kL[0][0] = 1.0f + fT2;
            kL[0][1] = kL[1][0] = fT2 * afV[1];
            kL[0][2] = kL[2][0] = fT2 * afV[2];
            kL[1][1] = 1.0f + fT2 * afV[1] * afV[1];
            kL[1][2] = kL[2][1] = fT2 * afV[1] * afV[2];
            kL[2][2] = 1.0f + fT2 * afV[2] * afV[2];
            identity = false;
        }
        else
        {
            kL = Matrix3::Identity;
            identity = true;
        }

        length = ::sqrtf(kA[0][1] * kA[0][1] + kA[0][2] * kA[0][2]);
        if (length > 0.0f)
        {
            fSign = (kA[0][1] > 0.0f ? 1.0f : -1.0f);
            fT1 = kA[0][1] + fSign * length;
            afV[2] = kA[0][2] / fT1;

            fT2 = -2.0f / (1.0f + afV[2] * afV[2]);
            afW[0] = fT2 * (kA[0][1] + kA[0][2] * afV[2]);
            afW[1] = fT2 * (kA[1][1] + kA[1][2] * afV[2]);
            afW[2] = fT2 * (kA[2][1] + kA[2][2] * afV[2]);
            kA[0][1] += afW[0];
            kA[1][1] += afW[1];
            kA[1][2] += afW[1] * afV[2];
            kA[2][1] += afW[2];
            kA[2][2] += afW[2] * afV[2];

            kR[0][0] = 1.0;
            kR[0][1] = kR[1][0] = 0.0;
            kR[0][2] = kR[2][0] = 0.0;
            kR[1][1] = 1.0f + fT2;
            kR[1][2] = kR[2][1] = fT2 * afV[2];
            kR[2][2] = 1.0f + fT2 * afV[2] * afV[2];
        }
        else
        {
            kR = Matrix3::Identity;
        }

        length = ::sqrtf(kA[1][1] * kA[1][1] + kA[2][1] * kA[2][1]);
        if (length > 0.0f)
        {
            fSign = (kA[1][1] > 0.0f ? 1.0f : -1.0f);
            fT1 = kA[1][1] + fSign * length;
            afV[2] = kA[2][1] / fT1;

            fT2 = -2.0f / (1.0f + afV[2] * afV[2]);
            afW[1] = fT2 * (kA[1][1] + kA[2][1] * afV[2]);
            afW[2] = fT2 * (kA[1][2] + kA[2][2] * afV[2]);
            kA[1][1] += afW[1];
            kA[1][2] += afW[2];
            kA[2][2] += afV[2] * afW[2];

            const auto fA = 1.0f + fT2;
            const auto fB = fT2 * afV[2];
            const auto fC = 1.0f + fB * afV[2];

            if (identity)
            {
                kL[0][0] = 1.0;
                kL[0][1] = kL[1][0] = 0.0;
                kL[0][2] = kL[2][0] = 0.0;
                kL[1][1] = fA;
                kL[1][2] = kL[2][1] = fB;
                kL[2][2] = fC;
            }
            else
            {
                for (auto row = 0; row < 3; row++)
                {
                    const auto fTmp0 = kL[row][1];
                    const auto fTmp1 = kL[row][2];
                    kL[row][1] = fA * fTmp0 + fB * fTmp1;
                    kL[row][2] = fB * fTmp0 + fC * fTmp1;
                }
            }
        }
    }

    void Matrix3::GolubKahanStep(Matrix3& kA, Matrix3& kL, Matrix3& kR)
    {
	    const auto t11 = kA[0][1] * kA[0][1] + kA[1][1] * kA[1][1];
	    const auto t22 = kA[1][2] * kA[1][2] + kA[2][2] * kA[2][2];
	    const auto t12 = kA[1][1] * kA[1][2];
	    const auto trace = t11 + t22;
	    const auto diff = t11 - t22;
	    const auto discr = ::sqrtf(diff * diff + 4.0f * t12 * t12);
	    auto root1 = 0.5f * (trace + discr);
	    auto root2 = 0.5f * (trace - discr);

	    auto y = kA[0][0] - (::fabsf(root1 - t22) <= ::fabsf(root2 - t22) ? root1 : root2);
        auto z = kA[0][1];
        auto invLength = 1.0f / ::sqrtf(y * y + z * z);
        auto sin = z * invLength;
        auto cos = -y * invLength;

        auto tmp0 = kA[0][0];
        auto tmp1 = kA[0][1];
        kA[0][0] = cos * tmp0 - sin * tmp1;
        kA[0][1] = sin * tmp0 + cos * tmp1;
        kA[1][0] = -sin * kA[1][1];
        kA[1][1] *= cos;

        size_t row;
        for (row = 0; row < 3; row++)
        {
            tmp0 = kR[0][row];
            tmp1 = kR[1][row];
            kR[0][row] = cos * tmp0 - sin * tmp1;
            kR[1][row] = sin * tmp0 + cos * tmp1;
        }

        y = kA[0][0];
        z = kA[1][0];
        invLength = 1.0f / ::sqrtf(y * y + z * z);
        sin = z * invLength;
        cos = -y * invLength;

        kA[0][0] = cos * kA[0][0] - sin * kA[1][0];
        tmp0 = kA[0][1];
        tmp1 = kA[1][1];
        kA[0][1] = cos * tmp0 - sin * tmp1;
        kA[1][1] = sin * tmp0 + cos * tmp1;
        kA[0][2] = -sin * kA[1][2];
        kA[1][2] *= cos;

        size_t col;
        for (col = 0; col < 3; col++)
        {
            tmp0 = kL[col][0];
            tmp1 = kL[col][1];
            kL[col][0] = cos * tmp0 - sin * tmp1;
            kL[col][1] = sin * tmp0 + cos * tmp1;
        }

        y = kA[0][1];
        z = kA[0][2];
        invLength = 1.0f / ::sqrtf(y * y + z * z);
        sin = z * invLength;
        cos = -y * invLength;

        kA[0][1] = cos * kA[0][1] - sin * kA[0][2];
        tmp0 = kA[1][1];
        tmp1 = kA[1][2];
        kA[1][1] = cos * tmp0 - sin * tmp1;
        kA[1][2] = sin * tmp0 + cos * tmp1;
        kA[2][1] = -sin * kA[2][2];
        kA[2][2] *= cos;

        for (row = 0; row < 3; row++)
        {
            tmp0 = kR[1][row];
            tmp1 = kR[2][row];
            kR[1][row] = cos * tmp0 - sin * tmp1;
            kR[2][row] = sin * tmp0 + cos * tmp1;
        }

        // adjust left
        y = kA[1][1];
        z = kA[2][1];
        invLength = 1.0f / ::sqrtf(y * y + z * z);
        sin = z * invLength;
        cos = -y * invLength;

        kA[1][1] = cos * kA[1][1] - sin * kA[2][1];
        tmp0 = kA[1][2];
        tmp1 = kA[2][2];
        kA[1][2] = cos * tmp0 - sin * tmp1;
        kA[2][2] = sin * tmp0 + cos * tmp1;

        for (col = 0; col < 3; col++)
        {
            tmp0 = kL[col][1];
            tmp1 = kL[col][2];
            kL[col][1] = cos * tmp0 - sin * tmp1;
            kL[col][2] = sin * tmp0 + cos * tmp1;
        }
    }

    float Matrix3::MaxCubicRoot(float coefficients[3])
    {
        // quick out for uniform scale (triple root)
        const auto fOneThird = 1.0f / 3.0f;
        const auto fEpsilon = 1e-06f;
    	
        const auto disc = coefficients[2] * coefficients[2] - 3.0f * coefficients[1];
        if (disc <= fEpsilon)
            return -fOneThird * coefficients[2];

        // Compute an upper bound on roots of P(x).  This assumes that A^T*A
        // has been scaled by its largest entry.
        auto fX = 1.0f;
        auto poly = coefficients[0] + fX * (coefficients[1] + fX * (coefficients[2] + fX));
        if (poly < 0.0f)
        {
            // uses a matrix norm to find an upper bound on maximum root
            fX = ::fabsf(coefficients[0]);
            auto fTmp = 1.0f + ::fabsf(coefficients[1]);
            if (fTmp > fX)
                fX = fTmp;
        	
            fTmp = 1.0f + ::fabsf(coefficients[2]);
            if (fTmp > fX)
                fX = fTmp;
        }

        // Newton's method to find root
        const auto fTwoC2 = 2.0f * coefficients[2];
        for (auto i = 0; i < 16; i++)
        {
            poly = coefficients[0] + fX * (coefficients[1] + fX * (coefficients[2] + fX));
            if (::fabsf(poly) <= fEpsilon)
                return fX;

            const auto derived = coefficients[1] + fX * (fTwoC2 + 3.0f * fX);
            fX -= poly / derived;
        }

        return fX;
    }


    Vector3 operator*(const Vector3& vec, const Matrix3& mat)
    {
        Vector3 prod{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            prod[row] =
                vec[0] * mat.m[0][row] +
                vec[1] * mat.m[1][row] +
                vec[2] * mat.m[2][row];
        }
    	
        return prod;
    }

    Matrix3 operator*(float scalar, const Matrix3& mat)
    {
        Matrix3 prod{};
    	
        for (size_t row = 0; row < 3; row++)
        {
            for (size_t col = 0; col < 3; col++)
            {
                prod[row][col] = scalar * mat.m[row][col];
            }
        }
    	
        return prod;
    }
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cfloat>
#include <ostream>

#include "clamp.h"
#include "math_utils.h"
#include "radian.h"


namespace mmo
{
    /// This class represents a two-dimensional vector.
    class Vector2
    {
    public:
        static Vector2 Zero;
        static Vector2 UnitX;
        static Vector2 UnitY;
        static Vector2 UnitScale;
        static Vector2 NegativeUnitX;
        static Vector2 NegativeUnitY;

    public:
        float x, y;

    public:

        inline Vector2()
            : x(0.0f)
            , y(0.0f)
        {
        }
        inline Vector2(float inX, float inY)
            : x(inX)
            , y(inY)
        {
        }

        inline Vector2(Vector2 const& other)
            : x(other.x)
            , y(other.y)
        {
        }

        [[nodiscard]] float* Ptr() { return &x; }
        [[nodiscard]] const float* Ptr() const { return &x; }

        inline bool IsNAN() const
        {
            return x != x || y != y;
        }

        // Logarithmic
        inline Vector2& operator+=(Vector2 const& other)
        {
            x += other.x;
            y += other.y;

            return *this;
        }
        inline Vector2& operator-=(Vector2 const& other)
        {
            x -= other.x;
            y -= other.y;

            return *this;
        }
        inline Vector2& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;

            return *this;
        }
        inline Vector2& operator*=(const Vector2& v)
        {
            x *= v.x;
            y *= v.y;

            return *this;
        }
        inline Vector2& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;

            return *this;
        }
        inline Vector2& operator/=(const Vector2& v)
        {
            x /= v.x;
            y /= v.y;

            return *this;
        }

        inline Vector2 operator+(Vector2 const& b) const
        {
            Vector2 v = *this;
            v += b;
            return v;
        }

        inline Vector2 operator-(Vector2 const& b) const
        {
            Vector2 v = *this;
            v -= b;
            return v;
        }

        inline Vector2 operator*(float b) const
        {
            Vector2 v = *this;
            v *= b;
            return v;
        }

        inline Vector2 operator*(const Vector2& b) const
        {
            return Vector2(
                x * b.x,
                y * b.y);
        }

        inline Vector2 operator/(float b) const
        {
            Vector2 v = *this;
            v /= b;
            return v;
        }

        inline Vector2 operator/(const Vector2& b) const
        {
            Vector2 v = *this;
            v /= b;
            return v;
        }

        inline bool operator < (const Vector2& rhs) const
        {
            if (x < rhs.x && y < rhs.y)
                return true;
            return false;
        }

        inline bool operator > (const Vector2& rhs) const
        {
            if (x > rhs.x && y > rhs.y)
                return true;
            return false;
        }

        // Comparison
        inline bool operator==(Vector2 const& other) const
        {
            return x == other.x && y == other.y;
        }

        inline bool operator!=(Vector2 const& other) const
        {
            return x != other.x || y != other.y;
        }

        inline bool IsNearlyEqual(const Vector2& other, float epsilon = 1e-4f) const
        {
            return (fabsf(x - other.x) <= epsilon &&
                fabsf(y - other.y) <= epsilon);
        }

        // Inversion
        inline Vector2 operator-() const
        {
            return Vector2(-x, -y);
        }

        inline Vector2 operator!() const
        {
            return Vector2(!x, !y);
        }

        float operator [] (const size_t i) const
        {
            ASSERT(i < 2);
            return *(&x + i);
        }

        float& operator [] (const size_t i)
        {
            ASSERT(i < 2);
            return *(&x + i);
        }
        
        // Dot product
        inline Vector2 operator*(Vector2 const& b)
        {
            return {
                x * b.x,
                y * b.y
            };
        }

        inline Vector2 MidPoint(const Vector2& vec) const
        {
            return {
                (x + vec.x) * 0.5f,
                (y + vec.y) * 0.5f
            };
        }

    public:

        /// Calculates the dot product of this vector and another one.
        [[nodiscard]] float Dot(Vector2 const& other) const
        {
            return x * other.x + y * other.y;
        }

        [[nodiscard]] float AbsDot(const Vector2& vec) const
        {
            return fabs(x * vec.x) + fabs(y * vec.y);
        }

        /// Gets the length of this vector.
        [[nodiscard]] float GetLength() const
        {
            return sqrtf(GetSquaredLength());
        }

        /// Gets the squared length of this vector (more performant than GetLength, so
        /// it should be used instead of GetLength wherever possible).
        [[nodiscard]] float GetSquaredLength() const
        {
            return x * x + y * y;
        }

        /// Normalizes this vector.
        float Normalize()
        {
            const float length = GetLength();
            if (length > 0.0f)
            {
                x /= length;
                y /= length;
            }
            return length;
        }

        /// Returns a normalized copy of this vector.
        [[nodiscard]] Vector2 NormalizedCopy() const
        {
            float length = GetLength();
            if (length <= 0.0f) length = 0.0001f;
            //ASSERT(length > 0.0f);

            const Vector2 v = *this;
            return v / length;
        }

        [[nodiscard]] float GetDistanceTo(const Vector2& rhs) const
        {
            return (*this - rhs).GetLength();
        }

        [[nodiscard]] float GetSquaredDistanceTo(const Vector2& rhs) const
        {
            return (*this - rhs).GetSquaredLength();
        }

        /// Checks whether all components of this vector are valid numbers.
        [[nodiscard]] bool IsValid() const
        {
            // Note: if any of these is NaN for example, the == operator will always return
            // false, that's why we do checks against x == x etc. here
            return x == x && y == y;
        }
        
        [[nodiscard]] Radian AngleBetween(const Vector2& dest) const
        {
            float lenProduct = GetLength() * dest.GetLength();

            // Divide by zero check
            if(lenProduct < 1e-6f)
            {
                lenProduct = 1e-6f;	
            }

            float f = Dot(dest) / lenProduct;
            f = Clamp(f, -1.0f, 1.0f);
            return ACos(f);
        }

        [[nodiscard]] bool IsZeroLength() const
        {
            const float squaredLength = (x * x) + (y * y);
            return (squaredLength < (1e-06 * 1e-06));
        }

        void Ceil(const Vector2& other)
        {
            if (other.x > x) x = other.x;
            if (other.y > y) y = other.y;
        }

        [[nodiscard]] Vector2 Lerp(const Vector2& target, float t) const
        {
            if (t <= 0.0f) {
                return *this;
            }

            if (t >= 1.0f) {
                return target;
            }

            // NaN check
            ASSERT(t == t);
            ASSERT(x == x);
            ASSERT(y == y);
            ASSERT(target.x == target.x);
            ASSERT(target.y == target.y);
            return *this + (target - *this) * t;
        }
    };

    inline std::ostream& operator<<(std::ostream& o, const mmo::Vector2& b)
    {
        return o
            << "(" << b.x << ", " << b.y << ")";
    }

    inline io::Writer& operator<<(io::Writer& w, const mmo::Vector2& b)
    {
        return w
            << io::write<float>(b.x)
            << io::write<float>(b.y);
    }

    inline io::Reader& operator>>(io::Reader& r, mmo::Vector2& b)
    {
        return r
            >> io::read<float>(b.x)
            >> io::read<float>(b.y);
    }

    inline Vector2 TakeMinimum(const Vector2& a, const Vector2& b) 
    {
        return Vector2(
            std::min(a.x, b.x),
            std::min(a.y, b.y)
        );
    }

    inline Vector2 TakeMaximum(const Vector2& a, const Vector2& b) 
    {
        return Vector2(
            std::max(a.x, b.x),
            std::max(a.y, b.y)
        );
    }
}

#pragma once

#include <ostream>
#include <array>

#include "macros.h"

namespace mmo
{
	template <class T, std::size_t Dim>
	struct Vector
	{
		typedef T Type;
		typedef std::array<T, Dim> Coordinates;

		enum
		{
			Dimensions = Dim
		};

		Coordinates coordinates;

		Vector()
		{
			clear();
		}

		Vector(T a, T b)
		{
			static_assert(Dim >= 2, "Vector dimension has to be >= 2");

			coordinates[0] = a;
			coordinates[1] = b;

			for (std::size_t i = 2; i < Dim; ++i)
			{
				coordinates[i] = T();
			}
		}

		Vector(T a, T b, T c)
		{
			static_assert(Dim >= 3, "Vector dimension has to be >= 3");

			coordinates[0] = a;
			coordinates[1] = b;
			coordinates[2] = c;

			for (std::size_t i = 3; i < Dim; ++i)
			{
				coordinates[i] = T();
			}
		}

		Vector(T a, T b, T c, T d)
		{
			static_assert(Dim >= 4, "Vector dimension has to be >= 4");

			coordinates[0] = a;
			coordinates[1] = b;
			coordinates[2] = c;
			coordinates[3] = d;

			for (std::size_t i = 4; i < Dim; ++i)
			{
				coordinates[i] = T();
			}
		}

		void clear()
		{
			for (std::size_t i = 0; i < Dim; ++i)
			{
				coordinates[i] = T();
			}
		}

		std::size_t size() const
		{
			return Dim;
		}

		T &operator [] (std::size_t index)
		{
			ASSERT(index < Dim);
			return coordinates[index];
		}

		const T &operator [] (std::size_t index) const
		{
			ASSERT(index < Dim);
			return coordinates[index];
		}

		T length() const
		{
			using namespace std;
			return static_cast<T>(sqrt(lengthSq()));
		}

		T lengthSq() const
		{
			T sum = T(0);

			for (std::size_t i = 0; i < Dim; ++i)
			{
				sum += coordinates[i] * coordinates[i];
			}

			return sum;
		}

		void normalize()
		{
			const T inversedLength = T(1) / length();

			for (std::size_t i = 0; i < Dim; ++i)
			{
				coordinates[i] *= inversedLength;
			}
		}

		Vector normal() const
		{
			Vector norm = *this;
			norm.normalize();
			return norm;
		}

		T dot(const Vector &other)
		{
			T result = T();

			for (std::size_t i = 0; i < Dim; ++i)
			{
				result += operator[](i) * other[i];
			}

			return result;
		}

		Vector operator + () const
		{
			return *this;
		}

		Vector operator - () const
		{
			Vector temp;

			for (std::size_t i = 0; i < Dim; ++i)
			{
				temp[i] = -coordinates[i];
			}

			return temp;
		}

		T &x()
		{
			return operator[](0);
		}

		const T &x() const
		{
			return operator[](0);
		}

		void x(const T &x)
		{
			operator[](0) = x;
		}

		T &y()
		{
			return operator[](1);
		}

		const T &y() const
		{
			return operator[](1);
		}

		void y(const T &y)
		{
			operator[](1) = y;
		}

		T &z()
		{
			return operator[](2);
		}

		const T &z() const
		{
			return operator[](2);
		}

		void z(const T &z)
		{
			operator[](2) = z;
		}
	};


	template <class T, std::size_t Dim>
	Vector<T, Dim> &operator += (Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		for (std::size_t i = 0; i < Dim; ++i)
		{
			left[i] += right[i];
		}
		return left;
	}

	template <class T, std::size_t Dim>
	Vector<T, Dim> &operator -= (Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		for (std::size_t i = 0; i < Dim; ++i)
		{
			left[i] -= right[i];
		}
		return left;
	}

	template <class T, std::size_t Dim>
	Vector<T, Dim> operator + (const Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		Vector<T, Dim> temp(left);
		temp += right;
		return temp;
	}

	template <class T, std::size_t Dim>
	Vector<T, Dim> operator - (const Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		Vector<T, Dim> temp(left);
		temp -= right;
		return temp;
	}


	template <class T, std::size_t Dim, class U>
	Vector<T, Dim> &operator *= (Vector<T, Dim> &left, U right)
	{
		for (std::size_t i = 0; i < Dim; ++i)
		{
			left[i] *= right;
		}
		return left;
	}

	template <class T, std::size_t Dim, class U>
	Vector<T, Dim> &operator /= (Vector<T, Dim> &left, U right)
	{
		for (std::size_t i = 0; i < Dim; ++i)
		{
			left[i] /= right;
		}
		return left;
	}

	template <class T, std::size_t Dim, class U>
	Vector<T, Dim> operator * (const Vector<T, Dim> &left, U right)
	{
		Vector<T, Dim> temp(left);
		temp *= right;
		return temp;
	}

	template <class T, std::size_t Dim, class U>
	Vector<T, Dim> operator / (const Vector<T, Dim> &left, U right)
	{
		Vector<T, Dim> temp(left);
		temp /= right;
		return temp;
	}

	template <class T, std::size_t Dim, class U>
	Vector<T, Dim> operator * (U left, const Vector<T, Dim> &right)
	{
		Vector<T, Dim> temp(right);
		temp *= left;
		return temp;
	}


	template <class T, std::size_t Dim>
	bool operator < (const Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		return std::lexicographical_compare(
		           left.coordinates.begin(),
		           left.coordinates.end(),
		           right.coordinates.begin(),
		           right.coordinates.end(),
		           std::less<T>()
		       );
	}

	template <class T, std::size_t Dim>
	bool operator == (const Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		return (left.coordinates == right.coordinates);
	}

	template <class T, std::size_t Dim>
	bool operator != (const Vector<T, Dim> &left, const Vector<T, Dim> &right)
	{
		return !(left == right);
	}


	template <class T, std::size_t Dim>
	Vector<T, Dim> abs(const Vector<T, Dim> &vector)
	{
		auto copy = vector;

		for (std::size_t i = 0; i < Dim; ++i)
		{
			auto &e = copy[i];
			if (e < 0)
			{
				e = -e;
			}
		}

		return copy;
	}


	template <class T, class U, std::size_t N>
	Vector<T, N> vector_cast(const Vector<U, N> &other)
	{
		Vector<T, N> result;
		for (size_t i = 0; i < N; ++i)
		{
			result[i] = static_cast<T>(other[i]);
		}
		return result;
	}


	template <class T, std::size_t Dim>
	std::ostream &operator << (std::ostream &os, const Vector<T, Dim> &right)
	{
		std::size_t i = 0;
		const std::size_t max = Dim - 1;
		for (; i < max; ++i)
		{
			os << right[i] << ' ';
		}
		os << right[i];
		return os;
	}

	template <class T>
	T distanceInPlane(const Vector<T, 3> &first, const Vector<T, 3> &second)
	{
		return Vector<T, 2>(
		           first[0] - second[0],
		           first[2] - second[2]
		       ).length();
	}

	template <class T>
	Vector<T, 2> makeVector(T x, T y)
	{
		return Vector<T, 2>(x, y);
	}

	template <class T>
	Vector<T, 3> makeVector(T x, T y, T z)
	{
		return Vector<T, 3>(x, y, z);
	}

	template <class T>
	Vector<T, 4> makeVector(T w, T x, T y, T z)
	{
		return Vector<T, 4>(w, x, y, z);
	}
}

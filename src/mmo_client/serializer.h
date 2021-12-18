#pragma once

#include <memory>

namespace mmo
{
	class Serializer
	{
	public:

		virtual ~Serializer() = default;

		enum class Endian
		{
			Native,
			Big,
			Little
		};

	protected:


	public:

	};
}

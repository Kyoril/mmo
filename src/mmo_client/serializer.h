// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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

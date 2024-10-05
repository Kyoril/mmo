// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

namespace mmo
{

	namespace movement_type
	{
		enum Type
		{
			Walk = 0,
			Run = 1,
			Backwards = 2,
			Swim = 3,
			SwimBackwards = 4,
			Turn = 5,
			Flight = 6,
			FlightBackwards = 7,

			Count = 8
		};
	}

	typedef movement_type::Type MovementType;

}
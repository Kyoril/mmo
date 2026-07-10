#pragma once

namespace mmo
{
	namespace vendor_result
	{
		enum Type
		{
			VendorHasNoItems,

			VendorHostile,

			VendorTooFarAway,

			VendorIsDead,

			CantShopWhileDead
		};
	}

	namespace trainer_result
	{
		enum Type
		{
			FailedLevelTooLow,

			FailedNotEnoughMoney,

			/// The trainer is a class trainer for a different class than the player's active class.
			FailedWrongClass
		};
	}
}
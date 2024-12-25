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

			FailedNotEnoughMoney
		};
	}
}
#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace xp
	{
		static uint32 GetExpCutoffLevel(uint32 level)
		{
			if (level <= 5) 
			{
				return 0;
			}

			if (level <= 39)
			{
				return level - 5 - level / 10;
			}

			if (level <= 59)
			{
				return level - 1 - level / 5;
			}

			return level - 9;
		}

		static uint32 GetZeroDifference(uint32 level)
		{
			if (level < 8) {
				return 5;
			}
			if (level < 10) {
				return 6;
			}
			if (level < 12) {
				return 7;
			}
			if (level < 16) {
				return 8;
			}
			if (level < 20) {
				return 9;
			}
			if (level < 30) {
				return 11;
			}
			if (level < 40) {
				return 12;
			}
			if (level < 45) {
				return 13;
			}
			if (level < 50) {
				return 14;
			}
			if (level < 55) {
				return 15;
			}
			if (level < 60) {
				return 16;
			}
			return 17;
		}

		static uint32 GetXpDiff(uint32 level)
		{
			if (level < 29) {
				return 0;
			}
			if (level == 29) {
				return 1;
			}
			if (level == 30) {
				return 3;
			}
			if (level == 31) {
				return 6;
			}

			return 5 * (level - 30);
		}

		static float GetGroupXpRate(uint32 count, bool raid)
		{
			if (raid)
			{
				return 1.0f;
			}
			else
			{
				if (count <= 2) {
					return 1.0f;
				}
				if (count == 3) {
					return 1.166f;
				}
				if (count == 4) {
					return 1.3f;
				}

				return 1.4f;
			}
		}
	}
}
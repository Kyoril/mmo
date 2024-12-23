// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "path.h"

#include <algorithm>

namespace mmo
{
	namespace virtual_dir
	{
		void appendPath(Path &left, const Path &right)
		{
			if (!left.empty() &&
			        !right.empty())
			{
				size_t slashes = 0;

				if (left.back() == PathSeparator)
				{
					++slashes;
				}

				if (right.front() == PathSeparator)
				{
					++slashes;
				}

				switch (slashes)
				{
				case 0:
					left += PathSeparator;
					break;

				case 2:
					//GCC 4.6 does not know pop_back from C++11
					//left.pop_back();

					left.erase(left.end() - 1);
					break;

				default:
					break;
				}
			}

			left += right;
		}

		Path joinPaths(Path left, const Path &right)
		{
			appendPath(left, right);
			return left;
		}

		std::pair<Path, Path> splitLeaf(Path path)
		{
			std::pair<Path, Path> result;

			result.first = std::move(path);

			const auto separator = std::find(
			                           result.first.rbegin(),
			                           result.first.rend(),
			                           PathSeparator).base();

			result.second.assign(separator, result.first.end());
			result.first.erase(separator, result.first.end());
			return result;
		}

		std::pair<Path, Path> splitRoot(Path path)
		{
			std::pair<Path, Path> result;

			result.first = std::move(path);

			const auto separator = std::find(
			                           result.first.begin(),
			                           result.first.end(),
			                           PathSeparator);

			auto secondBegin = separator;
			if (secondBegin != result.first.end() &&
			        *secondBegin == PathSeparator)
			{
				++secondBegin;
			}
			result.second.assign(secondBegin, result.first.end());
			result.first.erase(separator, result.first.end());
			return result;
		}
	}
}

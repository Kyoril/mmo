// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// This struct makes a class inheriting from it non-copyable if not explicitly overridden.
	struct NonCopyable
	{
		// Default constructor and destructor behavior
		NonCopyable() = default;
		virtual ~NonCopyable() = default;

	private:
		// Delete copy constructor and assignment operator and make them private

		NonCopyable(const NonCopyable&) = delete;
		NonCopyable& operator=(const NonCopyable&) = delete;
	};
}

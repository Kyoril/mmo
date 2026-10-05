// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>

namespace mmo::updating
{
	struct IUpdateSource;
	struct UpdateURL;

	/// Network behaviour of a source opened by openSourceFromUrl. The defaults keep the
	/// historic behaviour: wait forever and never cancel.
	struct SourceOptions
	{
		/// How long a single network operation may stall before the read fails.
		/// Zero disables the limit. Only HTTPS sources honour it.
		std::chrono::milliseconds inactivityTimeout{ 0 };

		/// When set and it becomes true, running reads are aborted with UpdateCancelled.
		/// Only HTTPS sources can abort a transfer that is already in progress.
		const std::atomic<bool>* cancel = nullptr;
	};

	std::unique_ptr<IUpdateSource> openSourceFromUrl(const UpdateURL &url, const SourceOptions &options = SourceOptions());
}

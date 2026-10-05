// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

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

		/// Background connections that download announced small files ahead of time with
		/// HTTP pipelining (see IUpdateSource::prefetch). Zero disables prefetching.
		/// Only HTTPS sources support it.
		uint32 prefetchConnections = 0;

		/// Requests sent ahead on each prefetch connection before waiting for responses.
		uint32 pipelineDepth = 16;

		/// Only files up to this size are prefetched. For larger files the round trip
		/// pipelining saves is negligible next to the transfer itself.
		std::uintmax_t prefetchMaxFileSize = 512 * 1024;

		/// How many prefetched bytes may be held in memory at once, in flight or waiting
		/// to be read.
		std::uintmax_t prefetchBufferLimit = 32 * 1024 * 1024;
	};

	std::unique_ptr<IUpdateSource> openSourceFromUrl(const UpdateURL &url, const SourceOptions &options = SourceOptions());
}

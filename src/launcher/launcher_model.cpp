// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_model.h"

#include <algorithm>
#include <utility>

namespace mmo
{
	void LauncherModel::SetPhase(const UpdatePhase phase)
	{
		Mutate([phase](UpdateSnapshot& state)
		{
			state.phase = phase;
			state.playEnabled = phase == UpdatePhase::Ready;
			if (phase != UpdatePhase::Updating)
			{
				state.downloadSizeText.clear();
				state.downloadRateText.clear();
			}
		});
	}

	void LauncherModel::SetStatus(std::string text)
	{
		Mutate([&text](UpdateSnapshot& state)
		{
			state.statusText = std::move(text);
		});
	}

	void LauncherModel::SetNotice(std::string text)
	{
		Mutate([&text](UpdateSnapshot& state)
		{
			state.noticeText = std::move(text);
		});
	}

	void LauncherModel::SetProgress(const float progress)
	{
		Mutate([progress](UpdateSnapshot& state)
		{
			state.progress = progress < 0.0f ? -1.0f : std::clamp(progress, 0.0f, 1.0f);
		});
	}

	void LauncherModel::SetDownloadDetails(std::string size, std::string rate)
	{
		Mutate([&](UpdateSnapshot& state)
		{
			state.downloadSizeText = std::move(size);
			state.downloadRateText = std::move(rate);
		});
	}

	void LauncherModel::SetReady(std::string text)
	{
		Mutate([&text](UpdateSnapshot& state)
		{
			state.phase = UpdatePhase::Ready;
			state.statusText = std::move(text);
			state.noticeText.clear();
			state.downloadSizeText.clear();
			state.downloadRateText.clear();
			state.progress = 1.0f;
			state.playEnabled = true;
		});
	}

	void LauncherModel::SetFailed(std::string error)
	{
		Mutate([&error](UpdateSnapshot& state)
		{
			state.phase = UpdatePhase::Failed;
			state.downloadSizeText.clear();
			state.downloadRateText.clear();
			state.statusText = std::move(error);
			state.playEnabled = false;
		});
	}

	bool LauncherModel::TryGetSnapshot(UpdateSnapshot& out, uint32& lastVersion) const
	{
		// The common case is "nothing changed", and it costs a single relaxed load.
		const uint32 version = m_version.load(std::memory_order_acquire);
		if (version == lastVersion)
		{
			return false;
		}

		{
			const std::scoped_lock lock{ m_mutex };
			out = m_state;
		}

		lastVersion = version;
		return true;
	}
}

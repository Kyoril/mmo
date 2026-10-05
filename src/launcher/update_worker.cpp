// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// asio has to come before any windows header.
#include "asio/io_service.hpp"
#include "asio.hpp"

#include "update_worker.h"

#include "base/filesystem.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

#include "updater/open_source_from_url.h"
#include "updater/prepare_parameters.h"
#include "updater/prepare_progress_handler.h"
#include "updater/prepare_update.h"
#include "updater/retrying_update_source.h"
#include "updater/update_application.h"
#include "updater/update_errors.h"
#include "updater/update_parameters.h"
#include "updater/update_source.h"
#include "updater/update_url.h"
#include "updater/updater_progress_handler.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <vector>

#ifdef _WIN32
#	include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		/// Returns the absolute path of the running executable.
		std::string GetSelfExecutablePath()
		{
#ifdef _WIN32
			std::array<char, MAX_PATH> buffer = { {} };
			GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			return buffer.data();
#else
			return {};
#endif
		}

		/// The condition tags that select which files in the manifest apply to us.
		std::set<std::string> BuildConditionSet()
		{
			std::set<std::string> conditions;
#ifdef _WIN32
			conditions.insert("WINDOWS");
#elif defined(__APPLE__)
			conditions.insert("OSX");
#endif
			conditions.insert(sizeof(void*) == 8 ? "X64" : "X86");
			return conditions;
		}

		/// Formats a byte count for display, e.g. "512 KB" or "1.4 GB".
		std::string FormatByteSize(const std::uintmax_t bytes)
		{
			constexpr std::array<const char*, 4> units = { "KB", "MB", "GB", "TB" };

			double value = static_cast<double>(bytes) / 1024.0;
			size_t unit = 0;
			while (value >= 1024.0 && unit + 1 < units.size())
			{
				value /= 1024.0;
				++unit;
			}

			std::array<char, 32> buffer = { {} };
			std::snprintf(buffer.data(), buffer.size(), value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
			return buffer.data();
		}
	}

	/// Translates updater callbacks into model updates. Called from several worker
	/// threads at once, so it only forwards to the worker, which locks what it shares.
	///
	/// Both callbacks are also the updater's only regular checkpoints, which makes them
	/// the place to abort a run: the updater reports everything by throwing, and an
	/// UpdateCancelled thrown here unwinds out of the step or the prepare walk.
	struct ModelProgressHandler final
		: updating::IPrepareProgressHandler
		, updating::IUpdaterProgressHandler
	{
		explicit ModelProgressHandler(UpdateWorker& worker)
			: m_worker(worker)
		{
		}

		void updateFile(const std::string& name, const std::uintmax_t size, const std::uintmax_t loaded) override
		{
			if (m_worker.m_shouldQuit)
			{
				throw updating::UpdateCancelled();
			}

			m_worker.OnFileProgress(name, size, loaded);
		}

		void beginCheckLocalCopy(const std::string& name) override
		{
			if (m_worker.m_shouldQuit)
			{
				throw updating::UpdateCancelled();
			}

			m_worker.OnCheckLocalCopy();
		}

	private:
		UpdateWorker& m_worker;
	};

	UpdateWorker::UpdateWorker(LauncherModel& model, UpdateWorkerConfig config)
		: m_model(model)
		, m_config(std::move(config))
	{
	}

	UpdateWorker::~UpdateWorker()
	{
		Stop();
	}

	void UpdateWorker::Start()
	{
		ASSERT(!m_thread.joinable() && "The update worker is already running");
		m_thread = std::thread([this] { Run(); });
	}

	void UpdateWorker::RequestStop()
	{
		m_shouldQuit = true;
	}

	bool UpdateWorker::Stop(const std::chrono::milliseconds timeout)
	{
		RequestStop();

		// The thread only exists once Start has been called, and closing the window
		// before that is normal, so joining unconditionally would call terminate.
		if (!m_thread.joinable())
		{
			return true;
		}

		{
			std::unique_lock lock{ m_finishedMutex };
			const auto isFinished = [this] { return m_finished; };

			if (timeout == std::chrono::milliseconds::max())
			{
				m_finishedCondition.wait(lock, isFinished);
			}
			else if (!m_finishedCondition.wait_for(lock, timeout, isFinished))
			{
				return false;
			}
		}

		m_thread.join();
		return true;
	}

	void UpdateWorker::Finish()
	{
		if (onFinished)
		{
			onFinished();
		}

		{
			const std::scoped_lock lock{ m_finishedMutex };
			m_finished = true;
		}

		m_finishedCondition.notify_all();
	}

	void UpdateWorker::OnCheckLocalCopy()
	{
		uint64 checked = 0;
		{
			const std::scoped_lock lock{ m_progressMutex };
			checked = ++m_filesChecked;
		}

		// Checking runs much faster than the UI can show it; a refresh every few files
		// is plenty to show that something is happening.
		if (checked == 1 || checked % 16 == 0)
		{
			m_model.SetStatus("Checking local files... (" + std::to_string(checked) + ")");
		}
	}

	void UpdateWorker::OnFileProgress(const std::string& name, const std::uintmax_t size, const std::uintmax_t loaded)
	{
		const std::scoped_lock lock{ m_progressMutex };

		FileProgress& file = m_fileProgress[name];
		if (loaded > file.loaded)
		{
			m_updated += loaded - file.loaded;
			file.loaded = loaded;
		}

		if (!file.done && loaded >= size)
		{
			file.done = true;
			++m_filesDone;
			ILOG("Successfully loaded file " << name << " (Size: " << size << " bytes)");
		}

		PublishDownloadProgress();
	}

	void UpdateWorker::PublishDownloadProgress()
	{
		// The total is only known once the prepare step has finished. Report
		// indeterminate until then rather than dividing by zero.
		if (m_fileCount == 0)
		{
			m_model.SetStatus("Updating...");
			m_model.SetProgress(-1.0f);
			return;
		}

		const std::uintmax_t filesDone = std::min(m_filesDone, m_fileCount);

		std::string status = "Downloading files: " + std::to_string(filesDone) + " / " + std::to_string(m_fileCount);
		if (m_updateSize > 0)
		{
			status += "  (" + FormatByteSize(std::min(m_updated, m_updateSize)) + " / " + FormatByteSize(m_updateSize) + ")";
		}

		m_model.SetStatus(std::move(status));

		// Bytes describe large files well, the file count describes many small ones well.
		// Whichever is further behind is the honest number: it keeps the bar from sitting
		// at 100% while thousands of tiny files are still being written.
		const float byteProgress = m_updateSize > 0
			? static_cast<float>(static_cast<double>(m_updated) / static_cast<double>(m_updateSize))
			: 1.0f;
		const float fileProgress = static_cast<float>(static_cast<double>(filesDone) / static_cast<double>(m_fileCount));
		m_model.SetProgress(std::min(byteProgress, fileProgress));
	}

	void UpdateWorker::Run()
	{
		ILOG("Connecting to the update server...");
		m_model.SetPhase(UpdatePhase::Connecting);
		m_model.SetStatus("Connecting to the update server...");
		m_model.SetProgress(-1.0f);

		constexpr bool doUnpackArchives = false;

		ModelProgressHandler progressHandler(*this);

		// The updater library reports failure by throwing, so this boundary keeps its
		// try/catch even though new code in the launcher does not use exceptions.
		try
		{
			updating::SourceOptions sourceOptions;
			sourceOptions.inactivityTimeout = m_config.inactivityTimeout;
			sourceOptions.cancel = &m_shouldQuit;

			updating::RetryPolicy retryPolicy;
			retryPolicy.maxAttempts = m_config.maxDownloadAttempts;

			auto source = std::make_unique<updating::RetryingUpdateSource>(
				updating::openSourceFromUrl(updating::UpdateURL(m_config.sourceUrl), sourceOptions),
				retryPolicy,
				&m_shouldQuit);

			source->onRetry = [this](const std::string& path, const uint32 attempt, const uint32 maxAttempts, const std::string& error)
			{
				WLOG("Download of " << path << " failed (attempt " << attempt << " of " << maxAttempts << "): " << error);
				m_model.SetNotice("Connection problems - retrying (attempt " + std::to_string(attempt + 1) +
					" of " + std::to_string(maxAttempts) + "). Your internet connection may be unstable.");
			};
			source->onRecovered = [this](const std::string& path)
			{
				ILOG("Download of " << path << " succeeded after retrying");
				m_model.SetNotice(std::string());
			};

			ILOG("Preparing data...");
			m_model.SetPhase(UpdatePhase::Preparing);
			m_model.SetStatus("Checking for updates...");

			updating::PrepareParameters prepareParameters(
				std::move(source),
				BuildConditionSet(),
				doUnpackArchives,
				progressHandler);

			const auto preparedUpdate = updating::prepareUpdate(m_config.outputDir, prepareParameters);

			{
				const std::scoped_lock lock{ m_progressMutex };
				m_updateSize = preparedUpdate.estimates.updateSize;
				m_fileCount = preparedUpdate.estimates.fileCount;
			}

			DLOG("Download size: " << preparedUpdate.estimates.downloadSize);
			DLOG("Update size: " << preparedUpdate.estimates.updateSize);
			DLOG("Files to update: " << preparedUpdate.estimates.fileCount);

			updating::UpdateParameters updateParameters(
				std::move(prepareParameters.source),
				doUnpackArchives,
				progressHandler);

			const std::string selfExecutablePath = GetSelfExecutablePath();
			ASSERT(!selfExecutablePath.empty());

			if (m_config.selfUpdateEnabled)
			{
				const auto selfUpdate = updating::updateApplication(selfExecutablePath, preparedUpdate);
				if (selfUpdate.perform)
				{
					m_model.SetStatus("Updating the launcher...");
					selfUpdate.perform(updateParameters, nullptr, 0);

					// perform() has already spawned the replacement launcher, so this
					// instance only has to close, which the UI thread must do.
					if (onSelfUpdateFinished)
					{
						onSelfUpdateFinished();
					}

					Finish();
					return;
				}
			}

			ILOG("Updating files...");
			m_model.SetPhase(UpdatePhase::Updating);
			{
				const std::scoped_lock lock{ m_progressMutex };
				PublishDownloadProgress();
			}

			{
				asio::io_service dispatcher;

				// Only the first failure is reported: once the dispatcher is stopped, the
				// other threads still finish their current step and may fail in turn, and
				// their errors would overwrite the one that actually caused the stop.
				std::atomic<bool> failed{ false };
				const auto fail = [this, &dispatcher, &failed](const std::string& error, const bool connectionProblem)
				{
					if (failed.exchange(true))
					{
						return;
					}

					ELOG("Update step failed: " << error);
					m_model.SetFailed(error);
					m_model.SetNotice(connectionProblem
						? "Could not reach the update server. Please check your internet connection and restart the launcher."
						: std::string());
					dispatcher.stop();
				};

				for (const auto& step : preparedUpdate.steps)
				{
					// Capture the element's address, not the loop reference variable:
					// `step` itself dies each iteration while this handler runs later on
					// another thread.
					dispatcher.post([this, &dispatcher, &updateParameters, &selfExecutablePath, &fail, stepPtr = &step]()
					{
						try
						{
							if (!m_config.selfUpdateEnabled)
							{
								try
								{
									if (std::filesystem::equivalent(stepPtr->destinationPath, selfExecutablePath))
									{
										return;
									}
								}
								catch (const std::filesystem::filesystem_error&)
								{
									// The destination may not exist yet, which is not an error.
								}
							}

							while (!m_shouldQuit && stepPtr->step(updateParameters))
							{
								;
							}
						}
						catch (const updating::UpdateCancelled&)
						{
							dispatcher.stop();
						}
						catch (const updating::SourceUnavailableError& ex)
						{
							fail(ex.what(), true);
						}
						catch (const std::exception& ex)
						{
							fail(ex.what(), false);
						}
						catch (...)
						{
							fail("An unknown error occurred while updating.", false);
						}

						if (m_shouldQuit)
						{
							dispatcher.stop();
						}
					});
				}

				std::vector<std::thread> threads;
				std::generate_n(std::back_inserter(threads), std::max<size_t>(1, m_config.concurrency), [&dispatcher]()
				{
					return std::thread([&dispatcher] { dispatcher.run(); });
				});

				for (std::thread& thread : threads)
				{
					thread.join();
				}

				if (m_shouldQuit || failed)
				{
					if (m_shouldQuit)
					{
						ILOG("Update cancelled");
					}

					Finish();
					return;
				}
			}

			{
				const std::scoped_lock lock{ m_progressMutex };
				DLOG("Updated " << m_filesDone << " / " << m_fileCount << " files, "
					<< m_updated << " / " << m_updateSize << " bytes");
			}

			ILOG("Game is up-to-date!");
			m_model.SetReady("Game is up-to-date!");
		}
		catch (const updating::UpdateCancelled&)
		{
			ILOG("Update cancelled");
		}
		catch (const updating::SourceUnavailableError& ex)
		{
			ELOG(ex.what());
			m_model.SetFailed(ex.what());
			m_model.SetNotice("Could not reach the update server. Please check your internet connection and restart the launcher.");
		}
		catch (const std::exception& ex)
		{
			ELOG(ex.what());
			m_model.SetFailed(ex.what());
		}

		Finish();
	}
}

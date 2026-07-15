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
#include "updater/update_application.h"
#include "updater/update_parameters.h"
#include "updater/update_source.h"
#include "updater/update_url.h"
#include "updater/updater_progress_handler.h"

#include <algorithm>
#include <array>
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
	}

	/// Translates updater callbacks into model updates. Called from several worker
	/// threads at once, so it touches nothing but atomics and the model, both of which
	/// are safe to share.
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
			if (loaded >= size)
			{
				m_worker.m_updated += loaded;
				ILOG("Successfully loaded file " << name << " (Size: " << size << " bytes)");
			}

			m_worker.m_model.SetStatus("Updating...");

			// The total is only known once the prepare step has finished, but this same
			// handler runs during it. Report indeterminate until then rather than
			// dividing by zero.
			const std::uintmax_t total = m_worker.m_updateSize.load();
			m_worker.m_model.SetProgress(total == 0
				? -1.0f
				: static_cast<float>(m_worker.m_updated.load()) / static_cast<float>(total));
		}

		void beginCheckLocalCopy(const std::string& name) override
		{
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

	void UpdateWorker::Stop()
	{
		m_shouldQuit = true;

		// The thread only exists once Start has been called, and closing the window
		// before that is normal, so joining unconditionally would call terminate.
		if (m_thread.joinable())
		{
			m_thread.join();
		}
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
			auto source = updating::openSourceFromUrl(updating::UpdateURL(m_config.sourceUrl));

			ILOG("Preparing data...");
			m_model.SetPhase(UpdatePhase::Preparing);
			m_model.SetStatus("Checking for updates...");

			updating::PrepareParameters prepareParameters(
				std::move(source),
				BuildConditionSet(),
				doUnpackArchives,
				progressHandler);

			const auto preparedUpdate = updating::prepareUpdate(m_config.outputDir, prepareParameters);

			m_updateSize = preparedUpdate.estimates.updateSize;
			DLOG("Download size: " << preparedUpdate.estimates.downloadSize);
			DLOG("Update size: " << m_updateSize.load());

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

					return;
				}
			}

			ILOG("Updating files...");
			m_model.SetPhase(UpdatePhase::Updating);

			{
				asio::io_service dispatcher;

				for (const auto& step : preparedUpdate.steps)
				{
					// Capture the element's address, not the loop reference variable:
					// `step` itself dies each iteration while this handler runs later on
					// another thread.
					dispatcher.post([this, &dispatcher, &updateParameters, &selfExecutablePath, stepPtr = &step]()
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
						catch (const std::exception& ex)
						{
							ELOG("Update step failed: " << ex.what());
							m_model.SetFailed(ex.what());
							dispatcher.stop();
						}
						catch (...)
						{
							ELOG("Update step failed with a non-standard exception");
							m_model.SetFailed("An unknown error occurred while updating.");
							dispatcher.stop();
						}
					});
				}

				std::vector<std::thread> threads;
				std::generate_n(std::back_inserter(threads), m_config.concurrency, [&dispatcher]()
				{
					return std::thread([&dispatcher] { dispatcher.run(); });
				});

				for (std::thread& thread : threads)
				{
					thread.join();
				}
			}

			if (m_shouldQuit)
			{
				return;
			}

			DLOG("Updated " << m_updated.load() << " / " << m_updateSize.load() << " bytes");

			UpdateSnapshot snapshot;
			uint32 version = 0;
			m_model.TryGetSnapshot(snapshot, version);
			if (snapshot.phase != UpdatePhase::Failed)
			{
				ILOG("Game is up-to-date!");
				m_model.SetReady("Game is up-to-date!");
			}
		}
		catch (const std::exception& ex)
		{
			ELOG(ex.what());
			m_model.SetFailed(ex.what());
		}

		if (onFinished)
		{
			onFinished();
		}
	}
}

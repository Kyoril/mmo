// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_uploader.h"

#include <chrono>

namespace mmo
{
	BugReportUploader::BugReportUploader(Transport transport, Poster poster, OutcomeHandler onOutcome,
		std::vector<uint32> retryDelaysMs, const size_t maxQueueSize)
		: m_transport(std::move(transport))
		, m_poster(std::move(poster))
		, m_onOutcome(std::move(onOutcome))
		, m_retryDelaysMs(std::move(retryDelaysMs))
		, m_maxQueueSize(maxQueueSize)
	{
	}

	BugReportUploader::~BugReportUploader()
	{
		Stop();
	}

	void BugReportUploader::Start()
	{
		std::scoped_lock lock{ m_mutex };
		if (m_thread.joinable())
		{
			return;
		}

		m_stopping = false;
		m_thread = std::thread([this]() { Run(); });
	}

	void BugReportUploader::Stop()
	{
		{
			std::scoped_lock lock{ m_mutex };
			m_stopping = true;
			m_queue.clear();
		}
		m_wakeUp.notify_all();

		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	bool BugReportUploader::Enqueue(std::string body)
	{
		bool overflow = false;
		{
			std::scoped_lock lock{ m_mutex };
			if (m_queue.size() >= m_maxQueueSize)
			{
				m_queue.pop_front();
				overflow = true;
			}
			m_queue.push_back(std::move(body));
		}
		m_wakeUp.notify_one();

		if (overflow)
		{
			Report({ Outcome::Dropped, 0, "upload queue full, oldest report dropped" });
		}

		return !overflow;
	}

	size_t BugReportUploader::GetQueueSize() const
	{
		std::scoped_lock lock{ m_mutex };
		return m_queue.size();
	}

	void BugReportUploader::Run()
	{
		for (;;)
		{
			std::string body;
			{
				std::unique_lock lock{ m_mutex };
				m_wakeUp.wait(lock, [this]() { return m_stopping || !m_queue.empty(); });
				if (m_stopping)
				{
					return;
				}

				body = std::move(m_queue.front());
				m_queue.pop_front();
			}

			const size_t maxAttempts = m_retryDelaysMs.size() + 1;
			for (size_t attempt = 1; attempt <= maxAttempts; ++attempt)
			{
				std::string error;
				uint32 status = 0;
				try
				{
					status = m_transport(body, error);
				}
				catch (const std::exception& e)
				{
					status = 0;
					error = e.what();
				}

				if (status >= 200 && status < 300)
				{
					Report({ Outcome::Delivered, status, "" });
					break;
				}

				if (status >= 400 && status < 500)
				{
					Report({ Outcome::Dropped, status, "rejected by the bug API: " + error });
					break;
				}

				const std::string detail = status == 0 ? error : "server error " + std::to_string(status);
				Report({ Outcome::AttemptFailed, status, detail });

				if (attempt == maxAttempts)
				{
					Report({ Outcome::Dropped, status, "giving up after " + std::to_string(maxAttempts) + " attempts" });
					break;
				}

				// Wait before retrying; Stop() interrupts the wait.
				std::unique_lock lock{ m_mutex };
				if (m_wakeUp.wait_for(lock, std::chrono::milliseconds(m_retryDelaysMs[attempt - 1]), [this]() { return m_stopping; }))
				{
					return;
				}
			}
		}
	}

	void BugReportUploader::Report(Outcome outcome)
	{
		if (!m_poster || !m_onOutcome)
		{
			return;
		}

		m_poster([handler = m_onOutcome, outcome = std::move(outcome)]()
		{
			handler(outcome);
		});
	}
}

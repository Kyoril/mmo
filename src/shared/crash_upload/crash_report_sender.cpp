// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "crash_report_sender.h"

#include "base/crash_report_outbox.h"
#include "http_client/api_url.h"
#include "http_client/send_request.h"
#include "https_client/send_request.h"
#include "log/default_log_levels.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

#ifndef _WIN32
#	include <sys/utsname.h>
#endif

namespace mmo
{
	namespace
	{
		/// An upload is small, so a link that stays silent this long is not coming back.
		constexpr auto UploadInactivityTimeout = std::chrono::seconds(15);

		/// Reports older than this are marked instead of sent: whatever they describe has been
		/// fixed or reported again since, and the build they came from is long gone.
		constexpr auto MaxReportAge = std::chrono::hours(24 * 30);

		std::string DescribeOs()
		{
#ifdef _WIN32
			return "Windows";
#else
			utsname name = {};
			if (uname(&name) != 0)
			{
				return "Linux";
			}

			return std::string(name.sysname) + " " + name.release + " " + name.machine;
#endif
		}

		std::string MakeBoundary(const std::string& content)
		{
			std::random_device device;
			std::mt19937_64 generator(device());

			for (;;)
			{
				std::ostringstream boundary;
				boundary << "----MmoCrashReport" << std::hex << generator();
				if (content.find(boundary.str()) == std::string::npos)
				{
					return boundary.str();
				}
			}
		}

		template<class Request>
		Request MakeRequest(const ApiUrl& url, const std::string& body, const std::string& contentType)
		{
			Request request;
			request.host = url.host;
			request.document = url.path + "/api/reports";
			request.method = "POST";
			request.headers.emplace_back("Content-Type", contentType);
			request.headers.emplace_back("User-Agent", "MMORPG Server Crash Reporter");
			request.body = body;
			return request;
		}

		template<class Response>
		uint32 ReadStatus(Response& response, std::string& error)
		{
			if (response.status >= 400 && response.body)
			{
				const std::string text((std::istreambuf_iterator<char>(*response.body)), std::istreambuf_iterator<char>());
				error = text.substr(0, 512);
			}

			return response.status;
		}

		bool IsTooOld(const std::filesystem::path& report)
		{
			std::error_code error;
			const auto written = std::filesystem::last_write_time(report, error);
			if (error)
			{
				return false;
			}

			return std::filesystem::file_time_type::clock::now() - written > MaxReportAge;
		}
	}

	CrashReportSender::CrashReportSender(Config config, Poster poster, OutcomeHandler onOutcome, Transport transport)
		: m_config(std::move(config))
		, m_poster(std::move(poster))
		, m_onOutcome(std::move(onOutcome))
		, m_transport(std::move(transport))
	{
	}

	CrashReportSender::~CrashReportSender()
	{
		Stop();
	}

	void CrashReportSender::Start()
	{
		if (!m_config.enabled)
		{
			ILOG("Crash report upload is disabled");
			return;
		}

		if (!m_transport)
		{
			m_transport = MakeTransport(m_config.apiUrl, &m_stopping);
			if (!m_transport)
			{
				ELOG("Crash report upload disabled: invalid crash API url '" << m_config.apiUrl << "' (expected https://, http:// or file://)");
				return;
			}
		}

		if (m_worker.joinable())
		{
			return;
		}

		m_stopping = false;
		m_worker = std::thread([this]()
		{
			SendPendingReports(false);
		});
	}

	void CrashReportSender::Stop()
	{
		m_stopping = true;

		if (m_worker.joinable())
		{
			m_worker.join();
		}
	}

	size_t CrashReportSender::SendPending()
	{
		return SendPendingReports(true);
	}

	size_t CrashReportSender::SendPendingReports(const bool direct)
	{
		size_t handled = 0;
		size_t attempted = 0;

		for (const auto& report : FindUnsentCrashReports(m_config.reportDirectory, m_config.applicationName))
		{
			if (m_stopping)
			{
				break;
			}

			Outcome outcome;
			outcome.report = report;

			if (IsTooOld(report))
			{
				MarkCrashReportSent(report, "skipped: older than 30 days");
				outcome.kind = Outcome::Rejected;
				outcome.detail = "older than 30 days, not sent";
				Deliver(outcome, direct);
				++handled;
				continue;
			}

			if (attempted >= MaxReportsPerStart)
			{
				break;
			}
			++attempted;

			CrashUpload upload;
			upload.reportPath = report.string();
			{
				std::ifstream in(report, std::ios::binary);
				upload.reportText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			}

			if (upload.reportText.empty())
			{
				MarkCrashReportSent(report, "skipped: empty or unreadable");
				outcome.kind = Outcome::Rejected;
				outcome.detail = "empty or unreadable, not sent";
				Deliver(outcome, direct);
				++handled;
				continue;
			}

			upload.logPath = ReadCrashReportDetail(upload.reportText, CrashReportLogFileKey);
			if (!upload.logPath.empty())
			{
				upload.logTail = ReadFileTail(upload.logPath, MaxLogTailBytes);
			}

			const std::string reportVersion = ReadCrashReportDetail(upload.reportText, "Version");
			upload.appVersion = reportVersion.empty() ? m_config.appVersion : reportVersion;
			upload.osVersion = DescribeOs();
			upload.deviceModel = m_config.applicationName + (m_config.nodeName.empty() ? std::string() : " @ " + m_config.nodeName);
			upload.errorMessage = m_config.applicationName + " crash: " + ReadCrashReportReason(upload.reportText);
			upload.userInput = "Automatic server crash report from " + upload.deviceModel;

			const std::string content = BuildCrashUploadContent(upload);
			const std::string boundary = MakeBoundary(content);
			const std::string body = BuildCrashUploadBody(upload, boundary);

			std::string error;
			uint32 status = 0;
			try
			{
				status = m_transport(body, "multipart/form-data; boundary=" + boundary, error);
			}
			catch (const std::exception& e)
			{
				status = 0;
				error = e.what();
			}

			outcome.status = status;
			outcome.detail = error;

			if (status >= 200 && status < 300)
			{
				MarkCrashReportSent(report, "sent: HTTP " + std::to_string(status));
				outcome.kind = Outcome::Sent;
				++handled;
			}
			else if (IsRetryableCrashUploadStatus(status))
			{
				outcome.kind = Outcome::Deferred;
				Deliver(outcome, direct);

				// The API is down or overloaded; the remaining reports would fare no better.
				break;
			}
			else
			{
				MarkCrashReportSent(report, "rejected: HTTP " + std::to_string(status) + " " + error);
				outcome.kind = Outcome::Rejected;
				++handled;
			}

			Deliver(outcome, direct);
		}

		return handled;
	}

	CrashReportSender::Transport CrashReportSender::MakeTransport(const std::string& apiUrl, const std::atomic<bool>* cancel)
	{
		ApiUrl parsed;
		if (!ParseApiUrl(apiUrl, parsed))
		{
			return {};
		}

		if (parsed.scheme == "file")
		{
			return [directory = parsed.path](const std::string& body, const std::string& /*contentType*/, std::string& error) -> uint32
			{
				std::error_code ec;
				std::filesystem::create_directories(directory, ec);

				const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();

				// Several reports can go out within the same millisecond.
				static std::atomic<uint32> counter{ 0 };
				const auto file = std::filesystem::path(directory) /
					("crash-" + std::to_string(now) + "-" + std::to_string(++counter) + ".txt");

				std::ofstream out(file, std::ios::binary);
				if (!out)
				{
					error = "cannot write " + file.string();
					return 0;
				}

				out << body;
				return 201;
			};
		}

		if (parsed.scheme == "https")
		{
			return [parsed, cancel](const std::string& body, const std::string& contentType, std::string& error) -> uint32
			{
				net::https_client::RequestOptions options;
				options.inactivityTimeout = UploadInactivityTimeout;
				options.cancel = cancel;
				options.maxResponseBytes = 64 * 1024;

				auto response = net::https_client::sendRequest(parsed.host, parsed.port,
					MakeRequest<net::https_client::Request>(parsed, body, contentType), options);
				return ReadStatus(response, error);
			};
		}

		return [parsed](const std::string& body, const std::string& contentType, std::string& error) -> uint32
		{
			auto response = net::http_client::sendRequest(parsed.host, parsed.port,
				MakeRequest<net::http_client::Request>(parsed, body, contentType));
			return ReadStatus(response, error);
		};
	}

	void CrashReportSender::Deliver(const Outcome& outcome, const bool direct)
	{
		const OutcomeHandler handler = m_onOutcome ? m_onOutcome : OutcomeHandler(&CrashReportSender::LogOutcome);

		if (direct || !m_poster)
		{
			handler(outcome);
			return;
		}

		m_poster([handler, outcome]()
		{
			handler(outcome);
		});
	}

	void CrashReportSender::LogOutcome(const Outcome& outcome)
	{
		switch (outcome.kind)
		{
		case Outcome::Sent:
			ILOG("Uploaded crash report " << outcome.report.filename().string() << " (HTTP " << outcome.status << ")");
			break;
		case Outcome::Rejected:
			WLOG("Crash report " << outcome.report.filename().string() << " was not uploaded"
				<< (outcome.status ? " (HTTP " + std::to_string(outcome.status) + ")" : std::string()) << ": " << outcome.detail);
			break;
		case Outcome::Deferred:
			WLOG("Could not upload crash report " << outcome.report.filename().string()
				<< (outcome.status ? " (HTTP " + std::to_string(outcome.status) + ")" : std::string())
				<< ", will retry on next start: " << outcome.detail);
			break;
		}
	}
}

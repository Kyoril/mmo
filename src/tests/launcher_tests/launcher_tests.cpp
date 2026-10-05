// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "launcher_content.h"
#include "launcher_model.h"

#include <filesystem>
#include <fstream>

using namespace mmo;

TEST_CASE("Launcher content parses UTF-8 and CRLF metadata", "[launcher][content]")
{
	const auto articles = LauncherContent::Parse("\xEF\xBB\xBF"
												 "First\r\n2026-10-05\r\nSummary\r\n## Highlights\r\n- Gr\xC3\xBC\xC3\x9F"
												 "e\r\n---\r\nSecond\r\nToday\r\nAnother summary\r\nBody\r\n");
	REQUIRE(articles.size() == 2);
	CHECK(articles[0].title == "First");
	CHECK(articles[0].date == "2026-10-05");
	CHECK(articles[0].body.find("## Highlights\n") == 0);
	CHECK(articles[0].body.find('\r') == std::string::npos);
	CHECK(articles[1].title == "Second");
}

TEST_CASE("Malformed launcher content cannot replace usable articles", "[launcher][content]")
{
	CHECK(LauncherContent::Parse("").empty());
	CHECK(LauncherContent::Parse("Title\nDate\nSummary").empty());
	CHECK(LauncherContent::Parse("\nDate\nSummary\nBody").empty());
	CHECK(LauncherContent::Parse(std::string(256 * 1024 + 1, 'x')).empty());
	CHECK(LauncherContent::Parse(std::string("Title\nDate\nSummary\nBody\0Hidden", 31)).empty());
	const auto articles = LauncherContent::Parse("Bad\nDate\n\nBody\n---\nGood\nDate\nSummary\nBody");
	REQUIRE(articles.size() == 1);
	CHECK(articles[0].title == "Good");
}

TEST_CASE("Launcher article counts and metadata are bounded", "[launcher][content]")
{
	std::string text;
	for (int i = 0; i < 40; ++i)
	{
		text += std::string(300, 'x') + "\nDate\nSummary\nBody\n---\n";
	}
	const auto articles = LauncherContent::Parse(text);
	REQUIRE(articles.size() == 32);
	CHECK(articles[0].title.size() == 180);
}

TEST_CASE("Missing content keeps embedded offline articles", "[launcher][content]")
{
	LauncherContent content;
	content.Load("missing-launcher-content-directory");
	REQUIRE_FALSE(content.GetNews().empty());
	REQUIRE_FALSE(content.GetPatches().empty());
	CHECK(content.GetPatches().front().body.find("No game release notes") != std::string::npos);
}

TEST_CASE("Launcher content can be published without rebuilding", "[launcher][content]")
{
	const auto directory = std::filesystem::temp_directory_path() / "mmo-launcher-content-test";
	std::filesystem::create_directories(directory / "launcher_content");
	{
		std::ofstream news(directory / "launcher_content/news.txt");
		news << "Published news\nToday\nReal summary\nReal article";
		std::ofstream patches(directory / "launcher_content/patches.txt");
		patches << "Incomplete file";
	}
	LauncherContent content;
	content.Load(directory.string());
	REQUIRE(content.GetNews().size() == 1);
	CHECK(content.GetNews().front().title == "Published news");
	CHECK(content.GetPatches().front().title.find("Launcher ") == 0);
	std::error_code error;
	std::filesystem::remove(directory / "launcher_content/news.txt", error);
	std::filesystem::remove(directory / "launcher_content/patches.txt", error);
	std::filesystem::remove(directory / "launcher_content", error);
	std::filesystem::remove(directory, error);
}

TEST_CASE("Rechecking an installation disables Play immediately", "[launcher][model]")
{
	LauncherModel model;
	uint32 version = 0;
	UpdateSnapshot snapshot;
	model.SetReady("Ready");
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	REQUIRE(snapshot.playEnabled);
	model.SetPhase(UpdatePhase::Preparing);
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	CHECK_FALSE(snapshot.playEnabled);
	model.SetProgress(2.0f);
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	CHECK(snapshot.progress == 1.0f);
	model.SetFailed("Failed");
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	CHECK_FALSE(snapshot.playEnabled);
	CHECK_FALSE(model.TryGetSnapshot(snapshot, version));
}
TEST_CASE("Download details are cleared when leaving the download phase", "[launcher][model]")
{
	LauncherModel model;
	uint32 version = 0;
	UpdateSnapshot snapshot;
	for (const auto phase : {UpdatePhase::Preparing, UpdatePhase::Ready, UpdatePhase::Failed})
	{
		model.SetPhase(UpdatePhase::Updating);
		model.SetDownloadDetails("4.6 GB / 8.1 GB", "12.4 MB/s | ~4m 49s");
		REQUIRE(model.TryGetSnapshot(snapshot, version));
		CHECK(snapshot.downloadSizeText == "4.6 GB / 8.1 GB");
		CHECK_FALSE(snapshot.downloadRateText.empty());
		if (phase == UpdatePhase::Ready)
		{
			model.SetReady("Ready");
		}
		else if (phase == UpdatePhase::Failed)
		{
			model.SetFailed("Failed");
		}
		else
		{
			model.SetPhase(phase);
		}
		REQUIRE(model.TryGetSnapshot(snapshot, version));
		CHECK(snapshot.downloadSizeText.empty());
		CHECK(snapshot.downloadRateText.empty());
	}
}

#ifdef _WIN32
#include "update_worker.h"
#include <condition_variable>
#include <mutex>

TEST_CASE("Repair can restart a finished updater and recover a failed run", "[launcher][repair]")
{
	const auto directory = std::filesystem::temp_directory_path() / "mmo-launcher-repair-test";
	std::filesystem::create_directories(directory);
	const auto publishEmptyManifest = [&]()
	{
		std::ofstream manifest(directory / "list.txt");
		manifest << "version = 1\nroot = (type = \"fs\" name = \"\" entries = {})\n";
	};
	publishEmptyManifest();
	LauncherModel model;
	UpdateWorkerConfig config;
	config.sourceUrl = directory.string();
	config.outputDir = directory.string();
	config.selfUpdateEnabled = false;
	config.maxDownloadAttempts = 1;
	UpdateWorker worker(model, config);
	std::mutex mutex;
	std::condition_variable condition;
	int finishedRuns = 0;
	worker.onFinished = [&]()
	{
		const std::scoped_lock lock{mutex};
		++finishedRuns;
		condition.notify_all();
	};
	const auto awaitRun = [&](const int run)
	{
		std::unique_lock lock{mutex};
		return condition.wait_for(lock, std::chrono::seconds(5),
								  [&]
								  {
									  return finishedRuns >= run;
								  });
	};
	uint32 version = 0;
	UpdateSnapshot snapshot;
	worker.Start();
	REQUIRE(awaitRun(1));
	REQUIRE(worker.Stop(std::chrono::seconds(5)));
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	INFO(snapshot.statusText);
	CHECK(snapshot.phase == UpdatePhase::Ready);
	CHECK(snapshot.playEnabled);
	{
		std::ofstream manifest(directory / "list.txt");
		manifest << "version = 999";
	}
	REQUIRE(worker.Restart());
	REQUIRE(awaitRun(2));
	REQUIRE(worker.Stop(std::chrono::seconds(5)));
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	CHECK(snapshot.phase == UpdatePhase::Failed);
	CHECK_FALSE(snapshot.playEnabled);
	publishEmptyManifest();
	REQUIRE(worker.Restart());
	REQUIRE(awaitRun(3));
	REQUIRE(worker.Stop(std::chrono::seconds(5)));
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	INFO(snapshot.statusText);
	CHECK(snapshot.phase == UpdatePhase::Ready);
	CHECK(snapshot.noticeText.empty());
	std::error_code error;
	std::filesystem::remove(directory / "list.txt", error);
	std::filesystem::remove(directory, error);
}
#endif

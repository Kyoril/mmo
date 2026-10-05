// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "launcher_content_service.h"
#include "launcher_model.h"
#include <fstream>
#include <chrono>
#include <thread>

using namespace mmo;

namespace
{
	const std::string ManifestUrl = "https://content.example/launcher.json";
	const std::string ImageUrl = "https://content.example/hero-v1.png";
	const std::string Manifest =
		R"({"version":1,"hero":{"image":"https://content.example/hero-v1.png","title":"New season"},"news":[{"title":"Published news","date":"2026-10-05","summary":"Read during download","body":"## News\n- First item","image":"https://content.example/hero-v1.png"}],"patches":[{"title":"Patch 1","summary":"Changes","body":"Fixed issues"}]})";

	std::string pixelPng()
	{
		const std::string hex = "89504e470d0a1a0a0000000d4948445200000001000000010804000000b51c0c020000000b4944415478da6364f80f000105010127"
								"18e3660000000049454e44ae426082";
		std::string bytes;
		for (size_t i = 0; i < hex.size(); i += 2)
		{
			const auto digit = [](const char c)
			{
				return c <= '9' ? c - '0' : c - 'a' + 10;
			};
			bytes += static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1]));
		}
		return bytes;
	}

	struct CacheDirectory
	{
		std::filesystem::path path =
			std::filesystem::temp_directory_path() /
			("launcher-content-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		~CacheDirectory()
		{
			std::error_code error;
			const auto target = std::filesystem::absolute(path, error).lexically_normal();
			const auto root = std::filesystem::temp_directory_path().lexically_normal();
			if (!error && target.parent_path() == root && target.filename().string().find("launcher-content-test-") == 0)
			{
				std::filesystem::remove_all(target, error);
			}
		}
	};

	std::shared_ptr<const RemoteLauncherContent> awaitImage(LauncherContentService& service, const std::string& image = ImageUrl)
	{
		uint64 revision = 0;
		std::shared_ptr<const RemoteLauncherContent> content;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (std::chrono::steady_clock::now() < deadline)
		{
			service.TryGetContent(content, revision);
			if (content && content->images.count(image) != 0)
			{
				return content;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return {};
	}
} // namespace

TEST_CASE("Launcher JSON validates schema, URLs and article bounds", "[launcher][content]")
{
	LauncherManifest parsed;
	REQUIRE(LauncherContentService::ParseManifest(Manifest, parsed));
	CHECK(parsed.heroTitle == "New season");
	REQUIRE(parsed.news.size() == 1);
	CHECK(parsed.news[0].image == ImageUrl);
	CHECK(parsed.news[0].body == "## News\n- First item");
	for (const auto& invalid :
		 {std::string("{bad"), std::string("{\"version\":2}"), std::string("{\"version\":1,\"news\":42}"),
		  std::string("{\"version\":1,\"hero\":{\"image\":\"http://host/file.png\"}}"), std::string(256 * 1024 + 1, ' ')})
	{
		CHECK_FALSE(LauncherContentService::ParseManifest(invalid, parsed));
		CHECK(parsed.heroTitle == "New season");
	}
	CHECK_FALSE(LauncherContentService::IsHttpsUrl("https://user:password@host/image.png"));
	CHECK_FALSE(LauncherContentService::IsHttpsUrl("https://host/image.png\r\nInjected: yes"));
	CHECK_FALSE(LauncherContentService::IsHttpsUrl("file:///local/image.png"));
}

TEST_CASE("Launcher cache survives offline startup and skips unchanged artwork", "[launcher][content][cache]")
{
	CacheDirectory directory;
	std::atomic<int> downloads{0};
	{
		LauncherContentService service(
			[&](const std::string& url, size_t, const std::atomic<bool>&, std::string& bytes)
			{
				bytes = url == ManifestUrl ? Manifest : pixelPng();
				if (url != ManifestUrl)
				{
					++downloads;
				}
				return true;
			});
		service.Start(ManifestUrl, directory.path);
		auto content = awaitImage(service);
		REQUIRE(content);
		CHECK(content->images.at(ImageUrl)->GetWidth() == 1);
		service.Stop();
	}
	CHECK(downloads == 1);
	{
		LauncherContentService offline(
			[](const std::string&, size_t, const std::atomic<bool>&, std::string&)
			{
				return false;
			});
		offline.Start(ManifestUrl, directory.path);
		uint64 revision = 0;
		std::shared_ptr<const RemoteLauncherContent> content;
		REQUIRE(offline.TryGetContent(content, revision));
		CHECK(content->manifest->news[0].title == "Published news");
		CHECK(content->images.count(ImageUrl) == 1);
		offline.Stop();
	}
	{
		LauncherContentService service(
			[&](const std::string& url, size_t, const std::atomic<bool>&, std::string& bytes)
			{
				bytes = Manifest;
				if (url != ManifestUrl)
				{
					++downloads;
				}
				return true;
			});
		service.Start(ManifestUrl, directory.path);
		REQUIRE(awaitImage(service));
		uint64 revision = 0;
		std::shared_ptr<const RemoteLauncherContent> content;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (revision < 2 && std::chrono::steady_clock::now() < deadline)
		{
			service.TryGetContent(content, revision);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		REQUIRE(revision >= 2);
		service.Stop();
	}
	CHECK(downloads == 1);
	{
		std::string updated = Manifest;
		size_t position = 0;
		while ((position = updated.find("hero-v1.png", position)) != std::string::npos)
		{
			updated.replace(position, 11, "hero-v2.png");
			position += 11;
		}
		LauncherContentService service(
			[&](const std::string& url, size_t, const std::atomic<bool>&, std::string& bytes)
			{
				bytes = url == ManifestUrl ? updated : pixelPng();
				if (url != ManifestUrl)
				{
					++downloads;
				}
				return true;
			});
		service.Start(ManifestUrl, directory.path);
		REQUIRE(awaitImage(service, "https://content.example/hero-v2.png"));
		service.Stop();
	}
	CHECK(downloads == 2);
}

TEST_CASE("Malformed artwork retains readable content without caching a bad image", "[launcher][content][cache]")
{
	CacheDirectory directory;
	std::string imageBytes = "not a PNG";
	SECTION("Invalid PNG")
	{
	}
	SECTION("Excessive PNG dimensions")
	{
		imageBytes = pixelPng();
		imageBytes[16] = 0x7f;
	}
	SECTION("Excessive compressed bytes")
	{
		imageBytes.assign(8 * 1024 * 1024 + 1, 'x');
	}
	std::atomic<bool> requested{false};
	LauncherContentService service(
		[&](const std::string& url, size_t, const std::atomic<bool>&, std::string& bytes)
		{
		bytes = url == ManifestUrl ? Manifest : imageBytes;
			requested = url != ManifestUrl;
			return true;
		});
	service.Start(ManifestUrl, directory.path);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (!requested && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	REQUIRE(requested);
	service.Stop();
	uint64 revision = 0;
	std::shared_ptr<const RemoteLauncherContent> content;
	REQUIRE(service.TryGetContent(content, revision));
	CHECK(content->manifest->news[0].title == "Published news");
	CHECK(content->images.empty());
	LauncherContentService offline(
		[](const std::string&, size_t, const std::atomic<bool>&, std::string& bytes)
		{
			bytes = "{invalid manifest";
			return true;
		});
	offline.Start(ManifestUrl, directory.path);
	revision = 0;
	REQUIRE(offline.TryGetContent(content, revision));
	CHECK(content->manifest->news[0].title == "Published news");
	offline.Stop();
}

TEST_CASE("Content server failure has no updater side effects and cancellation ends a refresh", "[launcher][content]")
{
	CacheDirectory directory;
	std::atomic<bool> entered{false};
	LauncherContentService service(
		[&](const std::string&, size_t, const std::atomic<bool>& cancel, std::string&)
		{
			entered = true;
			while (!cancel)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			return false;
		});
	service.Start(ManifestUrl, directory.path);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (!entered && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	REQUIRE(entered);
	LauncherModel model;
	model.SetReady("Ready");
	uint32 version = 0;
	UpdateSnapshot snapshot;
	REQUIRE(model.TryGetSnapshot(snapshot, version));
	CHECK(snapshot.playEnabled);
	service.Stop();
	uint64 revision = 0;
	std::shared_ptr<const RemoteLauncherContent> content;
	CHECK_FALSE(service.TryGetContent(content, revision));
}

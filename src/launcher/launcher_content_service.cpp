// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_content_service.h"
#include "https_client/connection.h"
#include "updater/update_url.h"
#include "log/default_log_levels.h"
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <fstream>
#include <set>
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace mmo
{
	namespace
	{
		constexpr size_t ManifestLimit = 256 * 1024;
		constexpr size_t ImageLimit = 8 * 1024 * 1024;
		constexpr uint64 PixelLimit = 8 * 1024 * 1024;

		std::string cacheKey(const std::string& url)
		{
			unsigned char digest[SHA256_DIGEST_LENGTH];
			SHA256(reinterpret_cast<const unsigned char*>(url.data()), url.size(), digest);
			const char* hex = "0123456789abcdef";
			std::string result;
			for (const auto value : digest)
			{
				result += hex[value >> 4];
				result += hex[value & 15];
			}
			return result;
		}

		bool readFile(const std::filesystem::path& path, const size_t limit, std::string& bytes)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() <= 0 || file.tellg() > static_cast<std::streamoff>(limit))
			{
				return false;
			}
			bytes.resize(static_cast<size_t>(file.tellg()));
			file.seekg(0);
			return static_cast<bool>(file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())));
		}

		bool writeFile(const std::filesystem::path& path, const std::string& bytes)
		{
			auto temporary = path;
			temporary += ".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				file.close();
				if (!file)
				{
					return false;
				}
			}
#ifdef _WIN32
			return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
			std::error_code error;
			std::filesystem::rename(temporary, path, error);
			return !error;
#endif
		}

		std::shared_ptr<const Bitmap> decodeImage(const std::string& bytes, uint64& pixels)
		{
			// Check dimensions before asking the decoder to allocate its RGBA output.
			if (bytes.size() < 33 || bytes.size() > ImageLimit || std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) != 0 ||
				std::memcmp(bytes.data() + 12, "IHDR", 4) != 0)
			{
				return {};
			}
			const auto read32 = [&](const size_t offset)
			{
				uint32 value = 0;
				for (size_t i = offset; i < offset + 4; ++i)
				{
					value = (value << 8) | static_cast<unsigned char>(bytes[i]);
				}
				return value;
			};
			const uint64 width = read32(16);
			const uint64 height = read32(20);
			if (width == 0 || height == 0 || width > 4096 || height > 4096 || width * height > PixelLimit - pixels)
			{
				return {};
			}
			auto bitmap = std::make_shared<Bitmap>();
			if (!Bitmap::DecodePng(reinterpret_cast<const uint8*>(bytes.data()), bytes.size(), *bitmap))
			{
				return {};
			}
			pixels += width * height;
			return bitmap;
		}

		std::vector<std::string> imageUrls(const LauncherManifest& manifest)
		{
			std::vector<std::string> result;
			const auto add = [&](const std::string& url)
			{
				if (!url.empty() && result.size() < 8 && std::find(result.begin(), result.end(), url) == result.end())
				{
					result.push_back(url);
				}
			};
			add(manifest.heroImage);
			for (const auto& article : manifest.news)
			{
				add(article.image);
			}
			for (const auto& article : manifest.patches)
			{
				add(article.image);
			}
			return result;
		}

		bool fetchHttps(const std::string& url, const size_t limit, const std::atomic<bool>& cancel, std::string& bytes)
		{
			if (!LauncherContentService::IsHttpsUrl(url) || cancel)
			{
				return false;
			}
			updating::UpdateURL address(url);
			net::https_client::RequestOptions options;
			options.cancel = &cancel;
			options.inactivityTimeout = std::chrono::seconds(5);
			options.maxResponseBytes = limit;
			net::https_client::Connection connection(address.host, address.port == 0 ? 443 : address.port, options);
			net::https_client::Request request;
			request.host = address.host;
			request.document = address.path;
			auto response = connection.Send(request);
			if (response.status != 200 || !response.body)
			{
				return false;
			}
			bytes.assign(std::istreambuf_iterator<char>(*response.body), std::istreambuf_iterator<char>());
			return !bytes.empty() && bytes.size() <= limit;
		}
	} // namespace

	bool LauncherContentService::IsHttpsUrl(const std::string& url)
	{
		if (url.size() > 2048 || url.compare(0, 8, "https://") != 0 || url.find_first_of("\\#") != std::string::npos ||
			std::any_of(url.begin(), url.end(),
						[](const unsigned char c)
						{
							return c <= 32 || c == 127;
						}))
		{
			return false;
		}
		const auto slash = url.find('/', 8);
		const auto host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
		if (host.empty() || host.find_first_of("@?") != std::string::npos)
		{
			return false;
		}
		const auto colon = host.find(':');
		const auto hostname = host.substr(0, colon);
		if (hostname.empty() || !std::all_of(hostname.begin(), hostname.end(),
											 [](const unsigned char c)
											 {
												 return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
														c == '.' || c == '-';
											 }))
		{
			return false;
		}
		if (colon != std::string::npos)
		{
			uint32 port = 0;
			if (colon + 1 == host.size() || host.size() - colon > 6)
			{
				return false;
			}
			for (size_t i = colon + 1; i < host.size(); ++i)
			{
				if (host[i] < '0' || host[i] > '9')
				{
					return false;
				}
				port = port * 10 + host[i] - '0';
			}
			if (port == 0 || port > 65535)
			{
				return false;
			}
		}
		return true;
	}

	bool LauncherContentService::ParseManifest(const std::string& text, LauncherManifest& manifest)
	{
		if (text.empty() || text.size() > ManifestLimit || text.find('\0') != std::string::npos)
		{
			return false;
		}
		bool tooDeep = false;
		const auto json = nlohmann::json::parse(
			text,
			[&](const int depth, nlohmann::json::parse_event_t, nlohmann::json&)
			{
				tooDeep = tooDeep || depth > 16;
				return depth <= 16;
			},
			false);
		if (tooDeep || json.is_discarded() || !json.is_object() || !json.contains("version") || !json["version"].is_number_integer() ||
			json["version"] != 1)
		{
			return false;
		}
		LauncherManifest parsed;
		const auto field = [](const nlohmann::json& object, const char* key, const size_t limit, std::string& value)
		{
			if (!object.contains(key))
			{
				return true;
			}
			if (!object[key].is_string())
			{
				return false;
			}
			value = object[key].get<std::string>();
			return value.size() <= limit && value.find('\0') == std::string::npos;
		};
		if (json.contains("hero"))
		{
			const auto& hero = json["hero"];
			if (!hero.is_object() || !field(hero, "image", 2048, parsed.heroImage) || !field(hero, "title", 180, parsed.heroTitle) ||
				!field(hero, "subtitle", 200, parsed.heroSubtitle) || (!parsed.heroImage.empty() && !IsHttpsUrl(parsed.heroImage)))
			{
				return false;
			}
		}
		const auto articles = [&](const char* key, std::vector<LauncherArticle>& output)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_array() || json[key].size() > 32)
			{
				return false;
			}
			for (const auto& entry : json[key])
			{
				LauncherArticle article;
				if (!entry.is_object() || !field(entry, "title", 180, article.title) || !field(entry, "date", 80, article.date) ||
					!field(entry, "summary", 400, article.summary) || !field(entry, "body", 64 * 1024, article.body) ||
					!field(entry, "image", 2048, article.image) || article.title.empty() || article.summary.empty() ||
					article.body.empty() || (!article.image.empty() && !IsHttpsUrl(article.image)))
				{
					return false;
				}
				output.push_back(std::move(article));
			}
			return true;
		};
		if (!articles("news", parsed.news) || !articles("patches", parsed.patches) ||
			(parsed.news.empty() && parsed.patches.empty() && parsed.heroImage.empty()))
		{
			return false;
		}
		parsed.revision = cacheKey(text);
		manifest = std::move(parsed);
		return true;
	}

	LauncherContentService::LauncherContentService(Fetch fetch) : m_fetch(fetch ? std::move(fetch) : fetchHttps)
	{
	}

	LauncherContentService::~LauncherContentService()
	{
		Stop();
	}

	void LauncherContentService::Stop()
	{
		m_cancel = true;
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	void LauncherContentService::Publish(const RemoteLauncherContent& content)
	{
		const std::scoped_lock lock{m_mutex};
		m_content = std::make_shared<RemoteLauncherContent>(content);
		++m_revision;
	}

	bool LauncherContentService::TryGetContent(std::shared_ptr<const RemoteLauncherContent>& content, uint64& revision) const
	{
		const std::scoped_lock lock{m_mutex};
		if (revision == m_revision || !m_content)
		{
			return false;
		}
		revision = m_revision;
		content = m_content;
		return true;
	}

	void LauncherContentService::Start(const std::string& url, const std::filesystem::path& cacheDirectory)
	{
		Stop();
		m_cancel = false;
		if (!IsHttpsUrl(url))
		{
			return;
		}
		const auto directory = cacheDirectory / cacheKey(url);
		std::string bytes;
		LauncherManifest manifest;
		if (readFile(directory / "launcher.json", ManifestLimit, bytes) && ParseManifest(bytes, manifest))
		{
			RemoteLauncherContent cached;
			cached.manifest = std::make_shared<LauncherManifest>(std::move(manifest));
			uint64 pixels = 0;
			for (const auto& image : imageUrls(*cached.manifest))
			{
				if (readFile(directory / (cacheKey(image) + ".png"), ImageLimit, bytes))
				{
					if (auto bitmap = decodeImage(bytes, pixels))
					{
						cached.images.emplace(image, std::move(bitmap));
					}
				}
			}
			Publish(cached);
		}
		m_thread = std::thread(
			[this, url, directory]
			{
				try
				{
					Refresh(url, directory);
				}
				catch (const std::exception&)
				{
					WLOG("Launcher content refresh unavailable; keeping cached or embedded content");
				}
			});
	}

	void LauncherContentService::Refresh(std::string url, std::filesystem::path directory)
	{
		std::string text;
		LauncherManifest manifest;
		if (!m_fetch(url, ManifestLimit, m_cancel, text) || m_cancel || !ParseManifest(text, manifest))
		{
			return;
		}
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		RemoteLauncherContent content;
		content.manifest = std::make_shared<LauncherManifest>(std::move(manifest));
		uint64 pixels = 0;
		const auto urls = imageUrls(*content.manifest);
		{
			const std::scoped_lock lock{m_mutex};
			if (m_content)
			{
				for (const auto& image : urls)
				{
					const auto found = m_content->images.find(image);
					if (found != m_content->images.end())
					{
						content.images.emplace(*found);
						pixels += static_cast<uint64>(found->second->GetWidth()) * found->second->GetHeight();
					}
				}
			}
		}
		// Publish readable text before waiting for image downloads.
		writeFile(directory / "launcher.json", text);
		Publish(content);
		for (const auto& image : urls)
		{
			if (m_cancel)
			{
				return;
			}
			const auto path = directory / (cacheKey(image) + ".png");
			if (content.images.count(image) != 0)
			{
				continue;
			}
			std::string bytes;
			std::shared_ptr<const Bitmap> bitmap;
			if (readFile(path, ImageLimit, bytes))
			{
				bitmap = decodeImage(bytes, pixels);
			}
			if (!bitmap)
			{
				try
				{
					if (m_fetch(image, ImageLimit, m_cancel, bytes) && !m_cancel)
					{
						bitmap = decodeImage(bytes, pixels);
						if (bitmap)
						{
							writeFile(path, bytes);
						}
					}
				}
				catch (const std::exception&)
				{
					WLOG("Launcher artwork unavailable; using embedded artwork");
				}
			}
			if (bitmap)
			{
				content.images.emplace(image, std::move(bitmap));
				Publish(content);
			}
		}
		if (!m_cancel && writeFile(directory / "launcher.json", text))
		{
			// Keep only this manifest's versioned image files; generated names never use server paths.
			std::set<std::string> retained;
			for (const auto& image : urls)
			{
				retained.insert(cacheKey(image) + ".png");
			}
			for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
			{
				const auto name = it->path().filename().string();
				if (it->path().extension() == ".png" && retained.count(name) == 0)
				{
					std::error_code removeError;
					std::filesystem::remove(it->path(), removeError);
				}
			}
		}
	}
} // namespace mmo

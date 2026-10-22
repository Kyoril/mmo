// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "assets/asset_registry.h"
#include "assets/shared_memory_stream.h"

#include <fstream>
#include <iterator>

using namespace mmo;

namespace
{
	/// An asset root in the temp directory holding a world entity that references a mesh, which references
	/// a material, which references a texture. The registry is torn down again on destruction.
	struct PrefetchFixture
	{
		std::filesystem::path root;

		PrefetchFixture()
		{
			root = std::filesystem::temp_directory_path() / "mmo_assets_prefetch_test";
			std::filesystem::remove_all(root);

			Write("Entities/1.wobj", Prefixed("Models/Rock.hmsh"));
			Write("Models/Rock.hmsh", "mesh" + Prefixed("Models/Rock.hmat"));
			Write("Models/Rock.hmat", "material" + Prefixed("Textures/Rock.htex"));
			Write("Textures/Rock.htex", "texture");

			AssetRegistry::Initialize(root, {});
		}

		~PrefetchFixture()
		{
			AssetRegistry::Destroy();
			std::filesystem::remove_all(root);
		}

		static std::string Prefixed(const std::string& name)
		{
			return std::string(1, static_cast<char>(name.size())) + std::string(1, '\0') + name;
		}

		void Write(const std::string& name, const std::string& content) const
		{
			const auto path = root / name;
			std::filesystem::create_directories(path.parent_path());
			std::ofstream(path, std::ios::binary) << content;
		}

		void RemoveFromDisk(const std::string& name) const
		{
			std::filesystem::remove(root / name);
		}

		static std::string Read(const std::string& name)
		{
			const auto file = AssetRegistry::OpenFile(name);
			if (!file)
			{
				return "<missing>";
			}

			return { std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>() };
		}
	};
}

TEST_CASE_METHOD(PrefetchFixture, "Prefetching a file reads everything it references, recursively", "[asset_prefetch]")
{
	AssetRegistry::PrefetchReferences("Entities/1.wobj");

	// Gone from disk, so only the prefetched copies can answer.
	RemoveFromDisk("Models/Rock.hmsh");
	RemoveFromDisk("Models/Rock.hmat");
	RemoveFromDisk("Textures/Rock.htex");

	CHECK(Read("Models/Rock.hmsh") == "mesh" + Prefixed("Models/Rock.hmat"));
	CHECK(Read("Models/Rock.hmat") == "material" + Prefixed("Textures/Rock.htex"));
	CHECK(Read("Textures/Rock.htex") == "texture");
}

TEST_CASE_METHOD(PrefetchFixture, "A prefetched copy is released by its first open", "[asset_prefetch]")
{
	AssetRegistry::Prefetch("Textures/Rock.htex");
	Write("Textures/Rock.htex", "changed");

	CHECK(Read("Textures/Rock.htex") == "texture");
	CHECK(Read("Textures/Rock.htex") == "changed");
}

TEST_CASE_METHOD(PrefetchFixture, "Files opened before are not prefetched again", "[asset_prefetch]")
{
	CHECK(Read("Textures/Rock.htex") == "texture");

	AssetRegistry::Prefetch("Textures/Rock.htex");
	Write("Textures/Rock.htex", "changed");

	CHECK(Read("Textures/Rock.htex") == "changed");
}

TEST_CASE_METHOD(PrefetchFixture, "Opened files are remembered, prefetched ones are not", "[asset_prefetch]")
{
	AssetRegistry::Prefetch("Textures/Rock.htex");
	CHECK_FALSE(AssetRegistry::WasOpened("Textures/Rock.htex"));

	Read("Textures/Rock.htex");
	CHECK(AssetRegistry::WasOpened("Textures/Rock.htex"));
	CHECK(AssetRegistry::WasOpened("Textures\\Rock.htex"));
}

TEST_CASE_METHOD(PrefetchFixture, "Cleared prefetches are read from disk again", "[asset_prefetch]")
{
	AssetRegistry::Prefetch("Textures/Rock.htex");
	AssetRegistry::ClearPrefetched();
	Write("Textures/Rock.htex", "changed");

	CHECK(Read("Textures/Rock.htex") == "changed");
}

TEST_CASE_METHOD(PrefetchFixture, "Prefetching a file that does not exist does nothing", "[asset_prefetch]")
{
	AssetRegistry::Prefetch("Textures/Missing.htex");

	CHECK(Read("Textures/Missing.htex") == "<missing>");
}

TEST_CASE("A shared memory stream reads and seeks like a file", "[asset_prefetch]")
{
	SharedMemoryStream stream(std::make_shared<const String>("0123456789"));

	char buffer[4] = {};
	stream.read(buffer, 3);
	CHECK(std::string(buffer, 3) == "012");
	CHECK(stream.tellg() == 3);

	stream.seekg(7, std::ios::beg);
	stream.read(buffer, 3);
	CHECK(std::string(buffer, 3) == "789");

	stream.seekg(-4, std::ios::end);
	stream.read(buffer, 1);
	CHECK(buffer[0] == '6');

	stream.seekg(-2, std::ios::cur);
	stream.read(buffer, 1);
	CHECK(buffer[0] == '5');

	stream.read(buffer, 4);
	CHECK(stream.gcount() == 4);
	stream.read(buffer, 4);
	CHECK(stream.eof());
}

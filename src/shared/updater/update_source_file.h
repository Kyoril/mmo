// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <istream>
#include <string>
#include <memory>
#include <optional>
#include <any>
#include <cstdint>

namespace mmo::updating
{
	struct UpdateSourceFile
	{
		//internalData must be first because 'content' may depend on it
		std::any internalData;
		std::unique_ptr<std::istream> content;
		std::optional<std::uintmax_t> size;


		UpdateSourceFile();
		UpdateSourceFile(
		    std::any internalData,
		    std::unique_ptr<std::istream> content,
		    std::optional<std::uintmax_t> size);
		UpdateSourceFile(UpdateSourceFile  &&other);
		UpdateSourceFile &operator = (UpdateSourceFile && other);
		void swap(UpdateSourceFile &other);
	};


	void checkExpectedFileSize(
	    const std::string &fileName,
	    std::uintmax_t expected,
	    const UpdateSourceFile &found
	);
}

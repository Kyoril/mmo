// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "update_source_file.h"


namespace mmo::updating
{
	UpdateSourceFile::UpdateSourceFile() = default;

	UpdateSourceFile::UpdateSourceFile(
	    std::any internalData,
	    std::unique_ptr<std::istream> content,
	    std::optional<std::uintmax_t> size)
		: internalData(std::move(internalData))
		, content(std::move(content))
		, size(size)
	{
	}

	UpdateSourceFile::UpdateSourceFile(UpdateSourceFile  &&other)
	{
		swap(other);
	}

	UpdateSourceFile &UpdateSourceFile::operator = (UpdateSourceFile && other)
	{
		swap(other);
		return *this;
	}

	void UpdateSourceFile::swap(UpdateSourceFile &other)
	{
		using std::swap;

		swap(internalData, other.internalData);
		swap(content, other.content);
		swap(size, other.size);
	}


	void checkExpectedFileSize(
	    const std::string &fileName,
	    std::uintmax_t expected,
	    const UpdateSourceFile &found
	)
	{
		if (found.size &&
		        *found.size != expected)
		{
			throw std::runtime_error(
			    fileName + ": Size expected to be " +
			    std::to_string(expected) +
			    " but found " +
			    std::to_string(*found.size));
		}
	}
}

#pragma once

#include "update_parameters.h"
#include "updater_progress_handler.h"


namespace mmo::updating
{
	void copyWithProgress(
	    const UpdateParameters &parameters,
	    std::istream &source,
	    std::ostream &sink,
	    const std::string &name,
	    std::uintmax_t compressedSize,
		std::uintmax_t originalSize,
	    bool doZLibUncompress
	);
}


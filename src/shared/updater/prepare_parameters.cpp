#include "prepare_parameters.h"
#include "update_source.h"


namespace mmo
{
	namespace updating
	{
		PrepareParameters::PrepareParameters(
		    std::unique_ptr<IUpdateSource> source,
		    std::set<std::string> conditionsSet,
		    bool doUnpackArchives,
		    IPrepareProgressHandler &progressHandler)
			: source(std::move(source))
			, conditionsSet(std::move(conditionsSet))
			, doUnpackArchives(doUnpackArchives)
			, progressHandler(progressHandler)
		{
		}

		PrepareParameters::~PrepareParameters()
		{
		}
	}
}

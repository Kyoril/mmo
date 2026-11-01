#pragma once

#include "base/typedefs.h"

namespace mmo
{
	class MaterialGraph;
	class GraphNode;

	struct NodeTypeInfo
	{
	    using Factory = GraphNode*(*)(MaterialGraph& material);

	    uint32 id;
	    std::string_view name;
	    std::string_view displayName;
	    Factory factory;
	};
	
}

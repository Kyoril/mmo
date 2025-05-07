#include "material_node.h"
#include "binary_io/reader.h"
#include "material_function.h"

namespace mmo
{
	ExpressionIndex MaterialFunctionInputNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
	{
        // TODO?
        return IndexNone;
	}

    io::Reader& MaterialFunctionInputNode::Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context)
    {
        GraphNode::Deserialize(reader, context);
        
        // Refresh output pins
        UpdatePinNames();
        
        return reader;
    }
}
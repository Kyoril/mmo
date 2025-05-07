#include "material_node.h"
#include "material_function.h"

namespace mmo
{
    ExpressionIndex MaterialFunctionOutputNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
    {
        // Make sure we have an input
        if (m_inputPins.empty() || !m_inputPins[0]->IsLinked())
        {
            // No input connected, return a default value
            return compiler.AddExpression("float3(0.0, 0.0, 0.0)", ExpressionType::Float_3);
        }

        // Get the connected pin and compile it
        const Pin* linkedPin = m_inputPins[0]->GetLink();
        GraphNode* linkedNode = linkedPin->GetNode();
        return linkedNode->Compile(compiler, linkedPin);
    }
}
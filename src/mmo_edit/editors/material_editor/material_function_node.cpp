#include "material_node.h"
#include "material_graph.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"
#include "scene_graph/material_manager.h"
#include "material_function_manager.h"
#include "material_function.h"
#include "log/default_log_levels.h"
#include "assets/asset_registry.h"
#include "base/chunk_reader.h"
#include "base/chunk_writer.h"

namespace mmo
{
    std::span<Pin*> MaterialFunctionNode::GetInputPins()
    {
        return m_inputPins;
    }

    std::span<Pin*> MaterialFunctionNode::GetOutputPins()
    {
        return m_outputPins;
    }

    std::string_view MaterialFunctionNode::GetName() const
    {
        return m_name;
    }

    ExpressionIndex MaterialFunctionNode::Compile(MaterialCompiler& compiler, const Pin* outputPin)
    {
        // If we have already compiled this for the specific output pin, return the cached value
        for (const auto& cache : m_compiledExpressionCache)
        {
            if (cache.outputPin == outputPin)
            {
                return cache.expressionId;
            }
        }

        // Skip if no material function is set
        if (m_materialFunctionPath.GetPath().empty())
        {
            WLOG("Material function node has no material function set");
            return IndexNone;
        }

        // Get the output pin index to determine which output we need
        int32 outputIndex = -1;
        for (size_t i = 0; i < m_outputPins.size(); ++i)
        {
            if (m_outputPins[i] == outputPin)
            {
                outputIndex = static_cast<int32>(i);
                break;
            }
        }

        if (outputIndex == -1)
        {
            ELOG("Invalid output pin for material function node");
            return IndexNone;
        }

        // Load the material function asset
        MaterialGraph* functionGraph = nullptr;
        ExecutableMaterialGraphLoadContext loadContext;

        // Load the material function file
        auto file = AssetRegistry::OpenFile(String(m_materialFunctionPath.GetPath()));
        if (!file)
        {
            ELOG("Failed to open material function file: " << m_materialFunctionPath.GetPath());
            return IndexNone;
        }

        // Create a new graph
        auto newGraph = std::make_unique<MaterialGraph>();
        functionGraph = newGraph.get();
        
        // Deserialize the graph
        io::StreamSource source(*file);
        io::Reader reader(source);
        
        // Create a deserializer
        ChunkReader chunkReader;
        chunkReader.SetIgnoreUnhandledChunks(true);
        
        // Add graph chunk handler
        chunkReader.AddChunkHandler(*ChunkMagic({'G', 'R', 'P', 'H'}), false, [&newGraph, &loadContext](io::Reader& reader, uint32, uint32) -> bool
        {
            return newGraph->Deserialize(reader, loadContext);
        });
        
        // Read the chunks
        if (!chunkReader.Read(reader))
        {
            ELOG("Failed to load material function graph from file: " << m_materialFunctionPath.GetPath());
            return IndexNone;
        }
        
        // Find the input and output nodes in the function graph
        std::vector<MaterialFunctionInputNode*> inputNodes;
        std::vector<MaterialFunctionOutputNode*> outputNodes;
        
        for (auto node : functionGraph->GetNodes())
        {
            if (auto* inputNode = dynamic_cast<MaterialFunctionInputNode*>(node))
            {
                inputNodes.push_back(inputNode);
            }
            else if (auto* outputNode = dynamic_cast<MaterialFunctionOutputNode*>(node))
            {
                outputNodes.push_back(outputNode);
            }
        }
        
        // Sort input nodes by their name to match our input pins order
        std::sort(inputNodes.begin(), inputNodes.end(), [](const MaterialFunctionInputNode* a, const MaterialFunctionInputNode* b) {
            return a->GetName() < b->GetName();
        });
        
        // Create a map of input node IDs to expression indices
        std::map<uint32, ExpressionIndex> inputExpressionMap;
        
        // Compile all input connections and map them to input nodes
        for (size_t i = 0; i < m_inputPins.size() && i < inputNodes.size(); ++i)
        {
            ExpressionIndex inputExpr = IndexNone;
            
            // If the input is connected, use the connected node's output
            if (m_inputPins[i]->IsLinked())
            {
                const Pin* linkedPin = m_inputPins[i]->GetLink();
                GraphNode* linkedNode = linkedPin->GetNode();
                inputExpr = linkedNode->Compile(compiler, linkedPin);
            }
            else
            {
                // Otherwise use the default value from the input node
                float defaultValue = inputNodes[i]->GetDefaultValue();
                inputExpr = compiler.AddExpression(std::to_string(defaultValue), ExpressionType::Float_1);
            }
            
            // Store the input expression for this input node
            inputExpressionMap[inputNodes[i]->GetId()] = inputExpr;
        }
        
        // Set up a node compiler wrapper that uses our input expressions
        class FunctionNodeCompiler
        {
        public:
            FunctionNodeCompiler(MaterialCompiler& compiler, const std::map<uint32, ExpressionIndex>& inputExpressions)
                : m_compiler(compiler)
                , m_inputExpressions(inputExpressions)
                , m_expressionCache()
            {
            }
            
            ExpressionIndex CompileNode(GraphNode* node, const Pin* outputPin)
            {
                // If this is a function input node, return the mapped expression
                if (auto* inputNode = dynamic_cast<MaterialFunctionInputNode*>(node))
                {
                    auto it = m_inputExpressions.find(inputNode->GetId());
                    if (it != m_inputExpressions.end())
                    {
                        return it->second;
                    }
                    
                    // If not found, let the input node compile itself with default values
                    return inputNode->Compile(m_compiler, outputPin);
                }
                
                // Otherwise, check the cache first
                auto cacheKey = std::make_pair(node->GetId(), outputPin->GetId());
                auto it = m_expressionCache.find(cacheKey);
                if (it != m_expressionCache.end())
                {
                    return it->second;
                }
                
                // If not cached, compile the node normally
                ExpressionIndex expression = node->Compile(m_compiler, outputPin);
                
                // Cache the result
                m_expressionCache[cacheKey] = expression;
                
                return expression;
            }
            
        private:
            MaterialCompiler& m_compiler;
            const std::map<uint32, ExpressionIndex>& m_inputExpressions;
            std::map<std::pair<uint32, uint32>, ExpressionIndex> m_expressionCache;
        };
        
        FunctionNodeCompiler functionCompiler(compiler, inputExpressionMap);
        
        // Find the matching output node for our output pin
        MaterialFunctionOutputNode* outputNode = nullptr;
        if (outputIndex < static_cast<int32>(outputNodes.size()))
        {
            // Sort output nodes to match our output pins order
            std::sort(outputNodes.begin(), outputNodes.end(), [](const MaterialFunctionOutputNode* a, const MaterialFunctionOutputNode* b) {
                return a->GetName() < b->GetName();
            });
            
            outputNode = outputNodes[outputIndex];
        }
        else
        {
            ELOG("Output pin index out of range for material function");
            return IndexNone;
        }
        
        // Make sure the output node has inputs
        auto inputPins = outputNode->GetInputPins();
        if (inputPins.empty() || !inputPins[0]->IsLinked())
        {
            WLOG("Material function output has no connected input");
            ExpressionIndex defaultExpr = compiler.AddExpression("float3(0.0, 0.0, 0.0)", ExpressionType::Float_3);
            m_compiledExpressionCache.push_back({outputPin, defaultExpr});
            return defaultExpr;
        }
        
        // Get the output node's connected pin
        const Pin* outputLinkedPin = inputPins[0]->GetLink();
        GraphNode* outputLinkedNode = outputLinkedPin->GetNode();
        
        // Compile the connected node
        ExpressionIndex resultExpr = functionCompiler.CompileNode(outputLinkedNode, outputLinkedPin);
        
        // Cache the result
        m_compiledExpressionCache.push_back({outputPin, resultExpr});
        
        return resultExpr;
    }

    void MaterialFunctionNode::RefreshPins()
    {
        // Clear existing pins
        for (auto pin : m_inputPins)
        {
            if (pin && pin->IsLinked())
            {
                pin->Unlink();
            }
            delete pin;
        }
        m_inputPins.clear();

        for (auto pin : m_outputPins)
        {
            if (pin && pin->IsLinked())
            {
                pin->Unlink();
            }
            delete pin;
        }
        m_outputPins.clear();

        // If no material function is set, return
        if (m_materialFunctionPath.GetPath().empty())
        {
            return;
        }

        // Try to load the material function
        auto materialFunction = MaterialFunctionManager::Get().Load(m_materialFunctionPath.GetPath());
        if (!materialFunction)
        {
            ELOG("Failed to load material function: " << m_materialFunctionPath.GetPath());
            return;
        }

        // Set the node name based on the function name
        m_name = std::filesystem::path(m_materialFunctionPath.GetPath()).filename().replace_extension().string();

        // Add input pins based on the material function's input parameters
        for (const auto& input : materialFunction->GetInputParams())
        {
            MaterialPin* inputPin = new MaterialPin(this, input.name);
            m_inputPins.push_back(inputPin);
        }

        // Add an output pin for the material function's output

        for (const auto& output : materialFunction->GetOutputs())
        {
            MaterialPin* outputPin = new MaterialPin(this, output.name);
            m_outputPins.push_back(outputPin);
        }
    }

    io::Writer& MaterialFunctionNode::Serialize(io::Writer& writer)
    {
        GraphNode::Serialize(writer);

        // Write the material function path
        writer << io::write_dynamic_range<uint16>(m_materialFunctionPath.GetPath());

        return writer;
    }

    io::Reader& MaterialFunctionNode::Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context)
    {
        GraphNode::Deserialize(reader, context);

        // Read the material function path
        std::string path;
        reader >> io::read_container<uint16>(path);
        m_materialFunctionPath.SetPath(path);

        // Update the material function path property
        m_materialFunctionChanged = m_materialFunctionPathProp.OnValueChanged.connect([this]() {
            RefreshPins();
        });

        // Refresh the pins based on the loaded material function
        RefreshPins();

        return reader;
    }
}

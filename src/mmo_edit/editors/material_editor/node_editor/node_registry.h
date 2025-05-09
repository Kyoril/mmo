#pragma once

#include "editors/material_editor/material_node.h"

#include "base/typedefs.h"

namespace mmo
{
	class MaterialGraph;

	class NodeRegistry
	{
	public:
	    NodeRegistry();

	public:
	    [[nodiscard]] uint32 RegisterNodeType(std::string_view name, std::string_view displayName, NodeTypeInfo::Factory factory);

		void RegisterNodeType(const NodeTypeInfo& typeInfo);

	    void UnregisterNodeType(std::string_view name);
		
	    [[nodiscard]] GraphNode* Create(uint32_t typeId, MaterialGraph& material) const;

	    [[nodiscard]] GraphNode* Create(std::string_view typeName, MaterialGraph& material) const;

	    [[nodiscard]] std::span<const NodeTypeInfo* const> GetTypes() const;

	private:
	    void RebuildTypes();

	    std::vector<NodeTypeInfo> m_BuildInNodes;

	    std::vector<NodeTypeInfo> m_CustomNodes;

	    std::vector<NodeTypeInfo*> m_Types;
	};
}

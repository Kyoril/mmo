#pragma once

#include <imgui_node_editor.h>

namespace mmo
{
    /// @brief This helper class allows to build new nodes in the node editor.
    class NodeBuilder final
	{
    public:
        /// @brief The pin id from which the new node will be created.
        ax::NodeEditor::PinId pinId = ax::NodeEditor::PinId::Invalid;

        /// @brief Tries to accept the new node item.
        /// @return true on success, false on error.
        [[nodiscard]] bool Accept() const
    	{
            return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
        }

        /// @brief Rejects the new node item.
        void Reject() const
    	{
            ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
    };
}

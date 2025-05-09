#pragma once

#include <imgui_node_editor.h>

namespace mmo
{
    /// @brief This helper class allows to build new links in the node editor.
    class LinkBuilder final
	{
    public:
        /// @brief The starting pin id for the new link.
        ax::NodeEditor::PinId startPinId = ax::NodeEditor::PinId::Invalid;
        /// @brief The ending pin id for the new link.
        ax::NodeEditor::PinId endPinId = ax::NodeEditor::PinId::Invalid;

        /// @brief Tries to accept the new link.
        /// @return true on success, false on error.
        [[nodiscard]] bool Accept() const {
            return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
        }

        /// @brief Rejects the new link.
        void Reject() const {
            ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        }
    };
}

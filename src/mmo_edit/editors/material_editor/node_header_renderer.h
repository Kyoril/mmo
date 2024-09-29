// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <functional>
#include <imgui.h>

namespace mmo
{
    /// @brief A helper class for rendering the header of a node to a ImDrawList instance.
    class NodeHeaderRenderer final
	{
    public:
	    /// @brief Shortcut for a draw function callback type.
	    using OnDrawCallback = std::function<void(ImDrawList* drawList)>;

    public:
	    /// @brief Creates a new instance of the NodeHeaderRenderer class and initializes it.
	    /// @param drawCallback Callback which performs the actual drawing.
	    NodeHeaderRenderer(OnDrawCallback drawCallback);

		/// @brief Destructor which performs cleanup.
	    ~NodeHeaderRenderer();

    public:
	    /// @brief Commits the draw operations into the draw list using the provided callback.
	    void Commit();

	    /// @brief Discards the draw operations.
	    void Discard();

	private:
	    ImDrawList* m_drawList = nullptr;
	    ImDrawListSplitter m_splitter;
	    OnDrawCallback m_drawCallback;
	};

}

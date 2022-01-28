// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <string_view>

namespace mmo
{
	/// @brief This class represents the response model of a link query. A link query is executed
	///	       when the user hovers over a target pin while dragging a new link from a source pin.
	///		   The result can be successful or false. If false, a reason can be provided which will
	///		   be displayed to the user.
	class LinkQueryResult final
	{
	public:
		/// @brief Creates a new instance of the LinkQueryResult class and initializes it.
		/// @param result Whether the link query indicates success, so a link can be created.
		/// @param reason A reason which will be displayed to the user in case the query result is false.
		LinkQueryResult(const bool result, const std::string_view reason = "")
	        : m_result(result)
	        , m_reason(reason)
	    {
	    }

	public:
	    /// @brief Shortcut for checking the query for success. Returns true on success.
	    explicit operator bool() const { return m_result; }

	public:
	    /// @brief Gets the result of the query. True on success, false on error.
	    /// @return The query result. True on success, false on error.
	    [[nodiscard]] bool GetResult() const { return m_result; }

	    /// @brief Returns the reason why the link query failed.
	    /// @return The reason why the link query failed.
	    [[nodiscard]] std::string_view GetReason() const { return m_reason; }

	private:
	    bool m_result = false;
	    std::string m_reason;
	};
	
}

// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "inventory_command.h"
#include "inventory_types.h"

#include <memory>
#include <string>
#include <chrono>
#include <vector>

namespace mmo
{
    /**
     * @brief Represents a log entry for an inventory command execution.
     */
    struct InventoryCommandLog
    {
        /// Timestamp when command was executed
        std::chrono::system_clock::time_point timestamp;

        /// Description of the command
        std::string description;

        /// Whether the command succeeded
        bool success;

        /// Error code if command failed (0 if success)
        uint8 errorCode;

        /// Execution duration in microseconds
        uint64 durationMicros;

        /**
         * @brief Constructs a command log entry.
         */
        InventoryCommandLog(
            std::chrono::system_clock::time_point ts,
            std::string desc,
            bool succeeded,
            uint8 error,
            uint64 duration) noexcept
            : timestamp(ts), description(std::move(desc)), success(succeeded), errorCode(error), durationMicros(duration)
        {
        }
    };

    /**
     * @brief Logger for inventory command execution.
     *
     * Provides command execution logging with:
     * - Timestamp tracking
     * - Success/failure recording
     * - Performance metrics
     * - History management
     * - Query capabilities
     *
     * This logger follows the Decorator pattern and can wrap command
     * execution to automatically capture execution metadata.
     */
    class InventoryCommandLogger
    {
    public:
        /**
         * @brief Constructs a logger with specified history size.
         * @param maxHistorySize Maximum number of log entries to retain (0 = unlimited).
         */
        explicit InventoryCommandLogger(size_t maxHistorySize = 100) noexcept;

    public:
        /**
         * @brief Executes a command and logs the result.
         * @param command The command to execute.
         * @return The result of command execution.
         */
        InventoryResult<void> ExecuteAndLog(IInventoryCommand &command);

        /**
         * @brief Gets the complete command history.
         * @return Vector of all logged command executions.
         */
        const std::vector<InventoryCommandLog> &GetHistory() const noexcept;

        /**
         * @brief Gets the most recent command log entry.
         * @return Pointer to last log entry, or nullptr if no history.
         */
        const InventoryCommandLog *GetLastLog() const noexcept;

        /**
         * @brief Gets number of successful commands executed.
         * @return Count of successful executions.
         */
        size_t GetSuccessCount() const noexcept;

        /**
         * @brief Gets number of failed commands executed.
         * @return Count of failed executions.
         */
        size_t GetFailureCount() const noexcept;

        /**
         * @brief Gets total number of commands executed.
         * @return Count of all executions.
         */
        size_t GetTotalCount() const noexcept;

        /**
         * @brief Clears all command history.
         */
        void ClearHistory() noexcept;

        /**
         * @brief Gets average execution duration in microseconds.
         * @return Average duration, or 0 if no history.
         */
        uint64 GetAverageDuration() const noexcept;

    private:
        /**
         * @brief Adds a log entry to history.
         * @param log The log entry to add.
         */
        void AddLog(InventoryCommandLog log);

    private:
        std::vector<InventoryCommandLog> m_history;
        size_t m_maxHistorySize;
        size_t m_successCount;
        size_t m_failureCount;
    };

    /**
     * @brief Decorator for commands that adds automatic logging.
     *
     * Wraps an existing command and delegates execution to the logger,
     * providing transparent logging without modifying command code.
     */
    class LoggedInventoryCommand final : public IInventoryCommand
    {
    public:
        /**
         * @brief Constructs a logged command wrapper.
         * @param command The command to wrap (transfers ownership).
         * @param logger The logger to use for execution.
         */
        LoggedInventoryCommand(
            std::unique_ptr<IInventoryCommand> command,
            InventoryCommandLogger &logger) noexcept;

    public:
        // IInventoryCommand implementation
        InventoryResult<void> Execute() override;
        const char *GetDescription() const noexcept override;

    private:
        std::unique_ptr<IInventoryCommand> m_command;
        InventoryCommandLogger &m_logger;
    };
}
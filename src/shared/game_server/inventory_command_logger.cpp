// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_command_logger.h"

#include <numeric>

namespace mmo
{
    InventoryCommandLogger::InventoryCommandLogger(size_t maxHistorySize) noexcept
        : m_maxHistorySize(maxHistorySize), m_successCount(0), m_failureCount(0)
    {
        if (m_maxHistorySize > 0)
        {
            m_history.reserve(m_maxHistorySize);
        }
    }

    InventoryResult<void> InventoryCommandLogger::ExecuteAndLog(IInventoryCommand &command)
    {
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto timestamp = std::chrono::system_clock::now();

        // Execute the command
        auto result = command.Execute();

        const auto endTime = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                                  endTime - startTime)
                                  .count();

        // Create log entry
        const bool success = !result.IsFailure();
        const uint8 errorCode = success ? 0 : result.GetError();

        AddLog(InventoryCommandLog(
            timestamp,
            command.GetDescription(),
            success,
            errorCode,
            static_cast<uint64>(duration)));

        return result;
    }

    const std::vector<InventoryCommandLog> &InventoryCommandLogger::GetHistory() const noexcept
    {
        return m_history;
    }

    const InventoryCommandLog *InventoryCommandLogger::GetLastLog() const noexcept
    {
        return m_history.empty() ? nullptr : &m_history.back();
    }

    size_t InventoryCommandLogger::GetSuccessCount() const noexcept
    {
        return m_successCount;
    }

    size_t InventoryCommandLogger::GetFailureCount() const noexcept
    {
        return m_failureCount;
    }

    size_t InventoryCommandLogger::GetTotalCount() const noexcept
    {
        return m_successCount + m_failureCount;
    }

    void InventoryCommandLogger::ClearHistory() noexcept
    {
        m_history.clear();
        m_successCount = 0;
        m_failureCount = 0;
    }

    uint64 InventoryCommandLogger::GetAverageDuration() const noexcept
    {
        if (m_history.empty())
        {
            return 0;
        }

        const uint64 totalDuration = std::accumulate(
            m_history.begin(),
            m_history.end(),
            uint64(0),
            [](uint64 sum, const InventoryCommandLog &log)
            {
                return sum + log.durationMicros;
            });

        return totalDuration / m_history.size();
    }

    void InventoryCommandLogger::AddLog(InventoryCommandLog log)
    {
        // Update counters
        if (log.success)
        {
            ++m_successCount;
        }
        else
        {
            ++m_failureCount;
        }

        // Add to history
        m_history.push_back(std::move(log));

        // Enforce max history size
        if (m_maxHistorySize > 0 && m_history.size() > m_maxHistorySize)
        {
            // Remove oldest entry
            m_history.erase(m_history.begin());
        }
    }

    // LoggedInventoryCommand implementation

    LoggedInventoryCommand::LoggedInventoryCommand(
        std::unique_ptr<IInventoryCommand> command,
        InventoryCommandLogger &logger) noexcept
        : m_command(std::move(command)), m_logger(logger)
    {
    }

    InventoryResult<void> LoggedInventoryCommand::Execute()
    {
        return m_logger.ExecuteAndLog(*m_command);
    }

    const char *LoggedInventoryCommand::GetDescription() const noexcept
    {
        return m_command->GetDescription();
    }
}
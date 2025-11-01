// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <vector>
#include <functional>

namespace mmo
{
    class IInventoryRepository;

    /**
     * @brief Unit of Work pattern for coordinating transactions.
     *
     * Coordinates changes across multiple repositories and ensures
     * all changes are committed or rolled back together. This follows
     * the Unit of Work pattern from Patterns of Enterprise Application
     * Architecture (Martin Fowler).
     *
     * Benefits:
     * - Transactional consistency across operations
     * - Simplified error handling
     * - Clear transaction boundaries
     * - Reduced coupling between domain and persistence
     */
    class InventoryUnitOfWork
    {
    public:
        /**
         * @brief Constructs a unit of work with a repository.
         * @param repository The inventory repository to coordinate.
         */
        explicit InventoryUnitOfWork(IInventoryRepository &repository) noexcept;

    public:
        /**
         * @brief Registers an action to be executed on commit.
         * @param action The action to register.
         */
        void RegisterNew(std::function<void()> action);

        /**
         * @brief Registers a dirty entity that needs to be updated.
         * @param action The update action to register.
         */
        void RegisterDirty(std::function<void()> action);

        /**
         * @brief Registers an entity to be deleted.
         * @param action The delete action to register.
         */
        void RegisterDeleted(std::function<void()> action);

        /**
         * @brief Commits all registered changes.
         * @return True if commit succeeded, false otherwise.
         */
        bool Commit();

        /**
         * @brief Rolls back all changes.
         * @return True if rollback succeeded, false otherwise.
         */
        bool Rollback();

        /**
         * @brief Checks if there are pending changes.
         * @return True if there are uncommitted changes.
         */
        bool HasChanges() const noexcept;

        /**
         * @brief Clears all pending changes without executing them.
         */
        void Clear() noexcept;

    private:
        IInventoryRepository &m_repository;
        std::vector<std::function<void()>> m_newActions;
        std::vector<std::function<void()>> m_dirtyActions;
        std::vector<std::function<void()>> m_deletedActions;
    };

    /**
     * @brief RAII wrapper for automatic transaction management.
     *
     * Ensures transactions are properly committed or rolled back,
     * even in the presence of exceptions.
     */
    class InventoryTransaction
    {
    public:
        /**
         * @brief Constructs a transaction and begins it.
         * @param repository The repository to use for the transaction.
         */
        explicit InventoryTransaction(IInventoryRepository &repository);

        /**
         * @brief Destructor - rolls back if not committed.
         */
        ~InventoryTransaction();

        // Non-copyable, non-movable
        InventoryTransaction(const InventoryTransaction &) = delete;
        InventoryTransaction &operator=(const InventoryTransaction &) = delete;
        InventoryTransaction(InventoryTransaction &&) = delete;
        InventoryTransaction &operator=(InventoryTransaction &&) = delete;

    public:
        /**
         * @brief Commits the transaction.
         * @return True if commit succeeded, false otherwise.
         */
        bool Commit();

        /**
         * @brief Rolls back the transaction.
         * @return True if rollback succeeded, false otherwise.
         */
        bool Rollback();

        /**
         * @brief Checks if transaction is active.
         * @return True if transaction is still active.
         */
        bool IsActive() const noexcept;

    private:
        IInventoryRepository &m_repository;
        bool m_active;
        bool m_committed;
    };
}
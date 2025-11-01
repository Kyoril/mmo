// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "inventory_unit_of_work.h"
#include "inventory_repository.h"

namespace mmo
{
       InventoryUnitOfWork::InventoryUnitOfWork(IInventoryRepository &repository) noexcept
           : m_repository(repository)
       {
       }

       void InventoryUnitOfWork::RegisterNew(std::function<void()> action)
       {
              m_newActions.push_back(std::move(action));
       }

       void InventoryUnitOfWork::RegisterDirty(std::function<void()> action)
       {
              m_dirtyActions.push_back(std::move(action));
       }

       void InventoryUnitOfWork::RegisterDeleted(std::function<void()> action)
       {
              m_deletedActions.push_back(std::move(action));
       }

       bool InventoryUnitOfWork::Commit()
       {
              if (!m_repository.BeginTransaction())
              {
                     return false;
              }

              try
              {
                     // Execute all registered actions
                     for (const auto &action : m_newActions)
                     {
                            action();
                     }

                     for (const auto &action : m_dirtyActions)
                     {
                            action();
                     }

                     for (const auto &action : m_deletedActions)
                     {
                            action();
                     }

                     // Commit transaction
                     if (!m_repository.Commit())
                     {
                            m_repository.Rollback();
                            return false;
                     }

                     // Clear actions after successful commit
                     Clear();
                     return true;
              }
              catch (...)
              {
                     m_repository.Rollback();
                     return false;
              }
       }

       bool InventoryUnitOfWork::Rollback()
       {
              Clear();
              return m_repository.Rollback();
       }

       bool InventoryUnitOfWork::HasChanges() const noexcept
       {
              return !m_newActions.empty() ||
                     !m_dirtyActions.empty() ||
                     !m_deletedActions.empty();
       }

       void InventoryUnitOfWork::Clear() noexcept
       {
              m_newActions.clear();
              m_dirtyActions.clear();
              m_deletedActions.clear();
       }

       // InventoryTransaction implementation

       InventoryTransaction::InventoryTransaction(IInventoryRepository &repository)
           : m_repository(repository), m_active(false), m_committed(false)
       {
              m_active = m_repository.BeginTransaction();
       }

       InventoryTransaction::~InventoryTransaction()
       {
              if (m_active && !m_committed)
              {
                     m_repository.Rollback();
              }
       }

       bool InventoryTransaction::Commit()
       {
              if (!m_active || m_committed)
              {
                     return false;
              }

              m_committed = m_repository.Commit();
              m_active = false;
              return m_committed;
       }

       bool InventoryTransaction::Rollback()
       {
              if (!m_active)
              {
                     return false;
              }

              m_active = false;
              return m_repository.Rollback();
       }

       bool InventoryTransaction::IsActive() const noexcept
       {
              return m_active;
       }
}
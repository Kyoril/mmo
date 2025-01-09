#pragma once

#include "Recast.h"

namespace mmo
{
    class RecastContext : public rcContext
    {
    private:
        const rcLogCategory m_logLevel;

        virtual void doLog(const rcLogCategory category, const char* msg, const int len) override;

    public:
        RecastContext(int logLevel)
            : m_logLevel(static_cast<rcLogCategory>(logLevel))
        {
        }
    };
}

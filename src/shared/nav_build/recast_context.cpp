
#include "recast_context.h"

#include <iomanip>
#include <iostream>
#include <thread>

#include "log/default_log_levels.h"

namespace mmo
{
	void RecastContext::doLog(const rcLogCategory category, const char* msg, const int len)
	{
        if (!m_logLevel || category < m_logLevel)
        {
            return;
        }
        
        switch (category)
        {
        case rcLogCategory::RC_LOG_ERROR:
            ELOG("Thread #" << std::setfill(' ') << std::setw(6) << std::this_thread::get_id() << ": " << msg);
            break;
        case rcLogCategory::RC_LOG_PROGRESS:
            ILOG("Thread #" << std::setfill(' ') << std::setw(6) << std::this_thread::get_id() << ": [PROGRESS] " << msg);
            break;
        case rcLogCategory::RC_LOG_WARNING:
            WLOG("Thread #" << std::setfill(' ') << std::setw(6) << std::this_thread::get_id() << ": " << msg);
            break;
        default:
            DLOG("Thread #" << std::setfill(' ') << std::setw(6) << std::this_thread::get_id() << ": " << msg);
            break;
        }
	}
}

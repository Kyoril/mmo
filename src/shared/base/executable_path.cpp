
#include "executable_path.h"

#ifdef _WIN32
#   include <Shlwapi.h>
#   pragma comment(lib, "shlwapi.lib")
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#   include <limits.h>
#   include "typedefs.h"
#endif

namespace mmo
{
    Path GetExecutablePath()
    {
#ifdef _WIN32
        char buf[MAX_PATH] = { 0 };
        DWORD length = GetModuleFileNameA( nullptr, buf, MAX_PATH );
        PathRemoveFileSpecA(buf);
        return buf;
#elif defined(__APPLE__)
        char buf [PATH_MAX];
        uint32 bufsize = PATH_MAX;
        if(!_NSGetExecutablePath(buf, &bufsize))
        {
            auto path = Path(buf).parent_path().parent_path().parent_path().parent_path();
            return path;
        }
        return Path();
#endif
    }
}

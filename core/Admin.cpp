#include "core/Admin.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pp {

bool isRunningAsAdmin()
{
#ifdef _WIN32
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    BOOL isAdmin = FALSE;

    if (!::AllocateAndInitializeSid(&ntAuthority,
                                    2,
                                    SECURITY_BUILTIN_DOMAIN_RID,
                                    DOMAIN_ALIAS_RID_ADMINS,
                                    0, 0, 0, 0, 0, 0,
                                    &adminGroup)) {
        return false;
    }

    if (!::CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
        isAdmin = FALSE;
    }

    ::FreeSid(adminGroup);
    return isAdmin != FALSE;
#else
    return false;
#endif
}

} // namespace pp


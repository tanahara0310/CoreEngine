#ifndef COREENGINE_APP_RESOURCE_H
#define COREENGINE_APP_RESOURCE_H

// exe に埋め込むアイコンの番号（CoreEngine.rc と共有する）
#define IDI_APP_ICON 101

#ifndef RC_INVOKED

#include <Windows.h>

namespace CoreEngine
{
    /// @brief exe に埋め込んだエンジンのアイコンを読む
    /// @param smallIcon true ならタイトルバー用の小さい大きさ、false なら Alt+Tab などの大きい大きさ
    /// @return 共有のアイコン（破棄しない）。読めなければ nullptr
    inline HICON LoadAppIcon(bool smallIcon)
    {
        const int width = ::GetSystemMetrics(smallIcon ? SM_CXSMICON : SM_CXICON);
        const int height = ::GetSystemMetrics(smallIcon ? SM_CYSMICON : SM_CYICON);
        return static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON, width, height, LR_DEFAULTCOLOR | LR_SHARED));
    }
}

#endif // RC_INVOKED

#endif // COREENGINE_APP_RESOURCE_H

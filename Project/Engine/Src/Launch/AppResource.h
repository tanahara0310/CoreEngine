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
    /// @return 初めて呼ばれたときに読み込み、以後は同じものを返す（破棄しない）。読めなければ nullptr
    inline HICON LoadAppIcon(bool smallIcon)
    {
        static HICON icons[2] = {};
        HICON& icon = icons[smallIcon ? 1 : 0];
        if (!icon) {
            const int width = ::GetSystemMetrics(smallIcon ? SM_CXSMICON : SM_CXICON);
            const int height = ::GetSystemMetrics(smallIcon ? SM_CYSMICON : SM_CYICON);
            icon = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
                IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
        }
        return icon;
    }
}

#endif // RC_INVOKED

#endif // COREENGINE_APP_RESOURCE_H

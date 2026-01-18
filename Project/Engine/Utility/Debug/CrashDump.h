#pragma once
#include <Windows.h>
#include <dbghelp.h>
#include <strsafe.h>
#pragma comment(lib, "Dbghelp.lib")


namespace CoreEngine
{
class CrashDump {
public:
    /// <summary>
    /// 例外フィルターを登録
    /// </summary>
    static void Register();

private: // メンバ変数
    // SHE(構造化例外)のコールバック関数
    static LONG WINAPI HandleException(EXCEPTION_POINTERS* exception);
};
}

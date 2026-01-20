#include "ProcessExecutor.h"
#include <vector>

namespace CoreEngine
{
	ProcessExecutor::ExecutionResult ProcessExecutor::Execute(const std::string& commandLine, bool hideWindow)
	{
		ExecutionResult result{};
		result.success = false;
		result.exitCode = 1;
		result.lastError = 0;

		// STARTUPINFO初期化
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		if (hideWindow)
		{
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
		}

		PROCESS_INFORMATION pi = {};

		// コマンドライン文字列を書き込み可能なバッファにコピー
		std::vector<char> cmdBuffer(commandLine.begin(), commandLine.end());
		cmdBuffer.push_back('\0');

		// プロセス作成
		BOOL createResult = CreateProcessA(
			nullptr,                // アプリケーション名（コマンドラインから取得）
			cmdBuffer.data(),       // コマンドライン
			nullptr,                // プロセスハンドル継承なし
			nullptr,                // スレッドハンドル継承なし
			FALSE,                  // ハンドル継承なし
			0,                      // 作成フラグなし
			nullptr,                // 親の環境変数を使用
			nullptr,                // 親のカレントディレクトリを使用
			&si,                    // STARTUPINFO
			&pi                     // PROCESS_INFORMATION
		);

		if (!createResult)
		{
			result.lastError = GetLastError();
			return result;
		}

		// プロセス完了を待機
		WaitForSingleObject(pi.hProcess, INFINITE);

		// 終了コードを取得
		DWORD dwExitCode = 0;
		if (GetExitCodeProcess(pi.hProcess, &dwExitCode))
		{
			result.exitCode = static_cast<int>(dwExitCode);
			result.success = true;
		}
		else
		{
			result.lastError = GetLastError();
		}

		// ハンドルをクローズ
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		return result;
	}
}

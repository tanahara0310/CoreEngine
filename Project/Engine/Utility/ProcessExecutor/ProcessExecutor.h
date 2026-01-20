#pragma once

#include <string>
#include <Windows.h>

namespace CoreEngine
{
	/// @brief 外部プロセス実行ユーティリティ
	class ProcessExecutor
	{
	public:
		/// @brief プロセス実行結果
		struct ExecutionResult
		{
			bool success;           ///< 実行成功フラグ
			int exitCode;           ///< 終了コード
			DWORD lastError;        ///< GetLastError()の結果（失敗時）

			/// @brief 実行が成功したか
			bool IsSuccess() const { return success && exitCode == 0; }
		};

		/// @brief 外部プロセスを実行
		/// @param commandLine コマンドライン文字列
		/// @param hideWindow ウィンドウを非表示にするか（デフォルト: true）
		/// @return 実行結果
		static ExecutionResult Execute(const std::string& commandLine, bool hideWindow = true);

	private:
		ProcessExecutor() = delete;
	};
}

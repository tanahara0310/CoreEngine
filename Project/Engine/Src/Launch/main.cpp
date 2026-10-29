#include "pch.h"
#include "MyGame.h"
#include "Utility/Path/ProjectPaths.h"

#pragma comment(lib, "delayimp.lib")

// Windowsアプリでのエントリーポイント(main関数)

using namespace CoreEngine;

namespace
{
	/// vcxproj の DelayLoadDLLs に並べた、exe の隣の Binaries から読む DLL
	constexpr const wchar_t* kBinariesDlls[] = { L"SDL3.dll", L"dxcompiler.dll" };

	/// @brief exe の隣の Binaries を DLL の探し先に足し、そこの DLL を読み込む
	/// @return すべて読めたか（読めなければメッセージを出してある）
	bool LoadBinaries()
	{
		std::array<wchar_t, MAX_PATH * 4> exePath{};
		const DWORD length = ::GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
		const std::filesystem::path binaries =
			std::filesystem::path(std::wstring(exePath.data(), length)).parent_path() / ProjectPaths::kBinariesFolder;
		::SetDllDirectoryW(binaries.c_str());

		for (const wchar_t* dll : kBinariesDlls) {
			if (!::LoadLibraryW(dll)) {
				const std::wstring message = std::wstring(dll) + L" を読み込めませんでした。\n" +
					binaries.wstring() + L" に置いてあるか確かめてください。";
				::MessageBoxW(nullptr, message.c_str(), L"CoreEngine", MB_OK | MB_ICONERROR);
				return false;
			}
		}
		return true;
	}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	if (!LoadBinaries()) {
		return 1;
	}

	std::unique_ptr<MyGame> game = std::make_unique<MyGame>();
	game->Run();

	return 0;
}

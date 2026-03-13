#include "Application/Src/MyGame.h"

// Windowsアプリでのエントリーポイント(main関数)

using namespace CoreEngine;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	std::unique_ptr<MyGame> game = std::make_unique<MyGame>();
	game->Run();

	return 0;
}

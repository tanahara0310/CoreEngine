#include "InputManager.h"
#include <cassert>

#include "GamepadInput.h"
#include "KeyboardInput.h"
#include "MouseInput.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
void InputManager::Initialize(HINSTANCE hInstance, HWND hwnd)
{

    HRESULT result;
    // DirectInputの初期化
    result = DirectInput8Create(
        hInstance,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        (void**)&directInput_,
        nullptr);

    assert(SUCCEEDED(result));

    // キーボードを生成して登録
    auto keyboard = std::make_unique<KeyboardInput>();
    keyboard->Initialize(directInput_, hwnd);
    devices_.push_back(std::move(keyboard)); // リストにキーボードを登録

    // マウスを生成して登録
    auto mouse = std::make_unique<MouseInput>();
    mouse->Initialize(directInput_, hwnd);
    devices_.push_back(std::move(mouse)); // リストにマウスを登録

    // ゲームパッドを生成して登録
    auto gamepad = std::make_unique<GamepadInput>();
    gamepad->Initialize(directInput_, hwnd);
    devices_.push_back(std::move(gamepad)); // リストにゲームパッドを登録
}

void InputManager::Update()
{

    // 各種デバイスの更新処理
    for (auto& device : devices_) {
        device->Update();
    }
}

KeyboardInput* InputManager::GetKeyboard()
{
    // リストの0番目がキーボード入力であることが前提
    return dynamic_cast<KeyboardInput*>(devices_[0].get());
}

MouseInput* InputManager::GetMouse()
{
    // リストの1番目がマウス入力であることが前提
    return dynamic_cast<MouseInput*>(devices_[1].get());
}

GamepadInput* InputManager::GetGamepad()
{
    // リストの2番目がゲームパッド入力であることが前提
    return dynamic_cast<GamepadInput*>(devices_[2].get());
}
}

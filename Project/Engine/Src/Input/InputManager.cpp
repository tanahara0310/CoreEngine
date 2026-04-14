#include "InputManager.h"
#include <cassert>

#include "GamepadInput.h"
#include "KeyboardInput.h"
#include "MouseInput.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
InputManager::InputManager() = default;
InputManager::~InputManager() = default;

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

    // 各デバイスの生成と初期化
    keyboard_ = std::make_unique<KeyboardInput>();
    keyboard_->Initialize(directInput_, hwnd);

    mouse_ = std::make_unique<MouseInput>();
    mouse_->Initialize(directInput_, hwnd);

    gamepad_ = std::make_unique<GamepadInput>();
    gamepad_->Initialize(directInput_, hwnd);

    // アクションベース入力クエリを初期化（デフォルトバインディングを適用）
    query_.Initialize(keyboard_.get(), mouse_.get(), gamepad_.get());
}

void InputManager::Update()
{
    if (keyboard_) keyboard_->Update();
    if (mouse_)    mouse_->Update();
    if (gamepad_)  gamepad_->Update();
}
}

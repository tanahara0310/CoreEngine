#include "pch.h"
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
    result = DirectInput8Create(
        hInstance,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()),
        nullptr);
    assert(SUCCEEDED(result));

    // 各デバイスを生成・初期化
    keyboard_ = std::make_unique<KeyboardInput>();
    keyboard_->Initialize(directInput_.Get(), hwnd);

    mouse_ = std::make_unique<MouseInput>();
    mouse_->Initialize(directInput_.Get(), hwnd);

    gamepad_ = std::make_unique<GamepadInput>();
    gamepad_->Initialize(0); // プレイヤー0番

    // 更新ループ用リストに登録（追加順で Update が呼ばれる）
    devices_.clear();
    devices_.push_back(keyboard_.get());
    devices_.push_back(mouse_.get());
    devices_.push_back(gamepad_.get());

    // アクションベース入力クエリを初期化
    query_.Initialize(keyboard_.get(), mouse_.get(), gamepad_.get());
}

void InputManager::Update()
{
    for (IInputDevice* device : devices_) {
        device->Update();
    }
}
}

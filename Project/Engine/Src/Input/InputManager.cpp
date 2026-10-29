#include "pch.h"
#include "InputManager.h"
#include <cassert>

#include "GamepadInput.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "EngineSystem/PlaybackState.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

#include <SDL3/SDL.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")


namespace CoreEngine
{
namespace
{
    const char* NameOrUnknown(const char* name)
    {
        return name ? name : "名前の無いパッド";
    }
}

InputManager::InputManager() = default;

InputManager::~InputManager()
{
    for (auto& pad : gamepads_) {
        if (!pad) {
            continue;
        }
        if (SDL_Gamepad* gamepad = pad->Detach()) {
            SDL_CloseGamepad(gamepad);
        }
    }
    if (gamepadSystemReady_) {
        SDL_Quit();
    }
}

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

    // 更新ループ用リストに登録（追加順で Update が呼ばれる）
    devices_.clear();
    devices_.push_back(keyboard_.get());
    devices_.push_back(mouse_.get());

    // パッドは SDL3 で読む（ゲームパッドの機能だけを使い、窓やイベントの処理はエンジンのまま）。
    // つながっているパッドは、最初の PollGamepadEvents でつないだものとして届く
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    gamepadSystemReady_ = SDL_Init(SDL_INIT_GAMEPAD);
    if (!gamepadSystemReady_) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Input,
            "ゲームパッドを読む準備ができませんでした（SDL_Init: {}）", SDL_GetError());
    }

    // パッドは 4 台分を作って毎フレーム見る。繋がっていない番号は何も返さない
    std::array<GamepadInput*, kMaxGamepads> pads{};
    for (int player = 0; player < kMaxGamepads; ++player) {
        gamepads_[player] = std::make_unique<GamepadInput>();
        gamepads_[player]->Initialize(static_cast<DWORD>(player));
        devices_.push_back(gamepads_[player].get());
        pads[player] = gamepads_[player].get();
    }

    // アクションベース入力クエリを初期化
    query_.Initialize(keyboard_.get(), mouse_.get(), pads);
}

void InputManager::Update()
{
    PollGamepadEvents();
    UpdateVibration();

    // 別のアプリを触っている間は取り込まない。
    // 取り込むと、切り替えた瞬間に押していたキーが押しっぱなしとして残り、
    // 戻ってきたときにメニューやキャラが勝手に動く
    if (!WinApp::IsAppActive()) {
        for (IInputDevice* device : devices_) {
            device->Reset();
        }
        return;
    }

    for (IInputDevice* device : devices_) {
        device->Update();
    }
}

void InputManager::PollGamepadEvents()
{
    if (!gamepadSystemReady_) {
        return;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            AttachGamepad(event.gdevice.which);
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            DetachGamepad(event.gdevice.which);
        }
    }
}

void InputManager::AttachGamepad(uint32_t instanceId)
{
    int slot = -1;
    const int preferred = SDL_GetGamepadPlayerIndexForID(instanceId);
    if (preferred >= 0 && preferred < kMaxGamepads && !gamepads_[preferred]->GetGamepad()) {
        slot = preferred;
    }
    for (int player = 0; slot < 0 && player < kMaxGamepads; ++player) {
        if (!gamepads_[player]->GetGamepad()) {
            slot = player;
        }
    }
    if (slot < 0) {
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Input,
            "ゲームパッドは {} 台までです。{} は使いません", kMaxGamepads, NameOrUnknown(SDL_GetGamepadNameForID(instanceId)));
        return;
    }

    SDL_Gamepad* const gamepad = SDL_OpenGamepad(instanceId);
    if (!gamepad) {
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Input,
            "ゲームパッドを開けませんでした（{}）", SDL_GetError());
        return;
    }
    gamepads_[slot]->Attach(gamepad);
    Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Input,
        "ゲームパッドをつなぎました：{}（{} 番）", NameOrUnknown(SDL_GetGamepadName(gamepad)), slot);
}

void InputManager::DetachGamepad(uint32_t instanceId)
{
    for (int player = 0; player < kMaxGamepads; ++player) {
        SDL_Gamepad* const gamepad = gamepads_[player]->GetGamepad();
        if (!gamepad || SDL_GetGamepadID(gamepad) != instanceId) {
            continue;
        }
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Input,
            "ゲームパッドを外しました：{}（{} 番）", NameOrUnknown(SDL_GetGamepadName(gamepad)), player);
        SDL_CloseGamepad(gamepads_[player]->Detach());
        return;
    }
}

void InputManager::UpdateVibration()
{
    const PlaybackStateManager& playback = PlaybackStateManager::GetInstance();
    const bool inPlayMode = playback.IsInPlayMode();
    const bool allowed = playback.IsPlaying() && WinApp::IsAppActive();
    for (auto& pad : gamepads_) {
        if (wasInPlayMode_ && !inPlayMode) {
            pad->SetVibration(0.0f, 0.0f);
        }
        pad->SetVibrationAllowed(allowed);
    }
    wasInPlayMode_ = inPlayMode;
}
}

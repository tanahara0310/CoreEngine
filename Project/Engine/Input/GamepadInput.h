#pragma once
#include "IInputDevice.h"
#include <Xinput.h>

/// @brief ゲームパッドのボタンを表す列挙型

namespace CoreEngine
{
enum class GamepadButton {

	A = XINPUT_GAMEPAD_A, // Aボタン
	B = XINPUT_GAMEPAD_B, // Bボタン
	X = XINPUT_GAMEPAD_X, // Xボタン
	Y = XINPUT_GAMEPAD_Y, // Yボタン
	DPadUp = XINPUT_GAMEPAD_DPAD_UP, // 上方向キー
	DPadDown = XINPUT_GAMEPAD_DPAD_DOWN, // 下方向キー
	DPadLeft = XINPUT_GAMEPAD_DPAD_LEFT, // 左方向キー
	DPadRight = XINPUT_GAMEPAD_DPAD_RIGHT, // 右方向キー
	Start = XINPUT_GAMEPAD_START, // スタートボタン
	Back = XINPUT_GAMEPAD_BACK, // バックボタン
	LeftThumb = XINPUT_GAMEPAD_LEFT_THUMB, // 左スティック押し込み
	RightThumb = XINPUT_GAMEPAD_RIGHT_THUMB, // 右スティック押し込み
	LeftShoulder = XINPUT_GAMEPAD_LEFT_SHOULDER, // 左ショルダーボタン
	RightShoulder = XINPUT_GAMEPAD_RIGHT_SHOULDER // 右ショルダーボタン
};

/// @brief スティックの状態を表す構造体
struct Stick {
	float x; // X軸の値
	float y; // Y軸の値
};

/// @brief ゲームパッド入力クラス
class GamepadInput : public IInputDevice {
public:
	/// @brief 初期化
	/// @param directInput DirectInputオブジェクト
	/// @param hwnd ウィンドウハンドル
	void Initialize(IDirectInput8* directInput, HWND hwnd) override;

	/// @brief ゲームパッドの状態を更新
	void Update() override;

	/// @brief ゲームパッドが接続されているかチェック
	/// @return 接続されている場合はtrue、そうでない場合はfalse
	bool IsConnected() const;

	/// @brief ボタンが押されているかどうかをチェック
	/// @param button 押されているかチェックするボタンのビットフラグ
	/// @return　押されている場合はtrue、そうでない場合はfalse
	bool IsButtonPressed(GamepadButton button) const;

	/// @brief　ボタンが押された瞬間かどうかをチェック
	/// @param button　押されたかチェックするボタンのビットフラグ
	/// @return 押された瞬間の場合はtrue、そうでない場合はfalse
	bool IsButtonTriggered(GamepadButton button) const;

	/// @brief　ボタンが離された瞬間かどうかをチェック
	/// @param button　離されたかチェックするボタンのビットフラグ
	/// @return　離された瞬間の場合はtrue、そうでない場合はfalse
	bool IsButtonReleased(GamepadButton button) const;

	/// @brief 左スティックの状態を取得
	/// @param deadZone スティックのデッドゾーン（デフォルトはXINPUT_GAMEPAD_LEFT_THUMB_DEADZONE）
	/// @return 左スティックの状態を表すStick構造体
	Stick GetLeftStick(float deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) const;

	/// @brief 右スティックの状態を取得
	/// @param deadZone スティックのデッドゾーン（デフォルトはXINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE）
	/// @return 右スティックの状態を表すStick構造体
	Stick GetRightStick(float deadZone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) const;

	/// @brief 振動を設定
	/// @param leftMotorRatio 左モーターの振動比率（0.0〜1.0）
	/// @param rightMotorRatio 右モーターの振動比率（0.0〜1.0）
	void SetVibration(float leftMotorRatio, float rightMotorRatio);

	/// @brief 左トリガーの値を取得
	/// @return 左トリガーの値（0.0〜1.0）
	float GetLeftTrigger() const;

	/// @brief 右トリガーの値を取得
	/// @return 右トリガーの値（0.0〜1.0）
	float GetRightTrigger() const;

private: // メンバ変数
	DWORD padIndex_ = 0; // プレイヤーインデックス（0〜3）
	XINPUT_STATE state_ {}; // 現在の状態
	XINPUT_STATE prevState_ {}; // 前フレームの状態
	bool isConnected_ = false; // 接続状態
};
}

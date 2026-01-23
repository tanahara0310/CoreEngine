#pragma once
#include "IInputDevice.h"
#include <cstdint>

/// @brief キーボード入力クラス

namespace CoreEngine
{
class KeyboardInput : public IInputDevice {
public:
	/// @brief 初期化
	/// @param directInput デバイス
	/// @param hwnd
	void Initialize(IDirectInput8* directInput, HWND hwnd) override;

	/// @brief 更新処理
	void Update() override;

	/// @brief キーが押されてるかどうか
	/// @param keyNumber キー番号 (0-255)
	/// @return
	bool IsKeyPressed(uint8_t keyNumber) const;

	/// @brief キーが押された瞬間かどうか
	/// @param keyNumber キー番号
	/// @return
	bool IsKeyTriggered(uint8_t keyNumber) const;

	/// @brief キーが離された瞬間かどうか
	/// @param keyNumber キー番号
	/// @return
	bool IsKeyReleased(uint8_t keyNumber) const;

private:
	IDirectInputDevice8* keyboard_ = nullptr; ///< キーボードデバイス
	BYTE key_[256] = {}; ///< 現在のキーの状態
	BYTE preKey_[256] = {}; ///< 1フレーム前のキーの状態
};
}

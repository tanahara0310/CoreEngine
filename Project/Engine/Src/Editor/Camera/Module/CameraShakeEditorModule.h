#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"

#include <string>

namespace CoreEngine
{
    /// @brief シェイクプリセットを編集し、その場で試すモジュール
    /// @details 揺れの実体は CameraShake::GetPresetLibrary() が持つ。シーケンスの
    ///          イベントトラックは名前だけを参照するので、ここで詰めた値が全カットへ効く。
    class CameraShakeEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "シェイク"; }

        /// @brief 毎フレーム更新（このモジュールは状態を進めない）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        /// @brief 選択中プリセットのパラメータ編集を描画
        /// @return 値が変わったら true
        bool DrawParameters(const CameraEditorContext& context);

        /// @brief 実行中の揺れと trauma の状態を描画
        void DrawRuntimeStatus();

        std::string selectedPresetName_;
        char newPresetNameBuffer_[64] = "";
        std::string statusMessage_;
    };
}

#endif // USE_IMGUI

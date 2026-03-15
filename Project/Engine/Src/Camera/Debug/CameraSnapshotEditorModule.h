#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"
#include "Camera/CameraStructs.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief エディター内スナップショットの保存/復元を担当するモジュール
    class CameraSnapshotEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "Snapshots"; }

        /// @brief 毎フレーム更新（現時点では状態更新なし）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        /// @brief アクティブ3Dカメラの状態をスナップショット化
        bool CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const;

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const;

    private:
        char snapshotNameBuffer_[128] = "Shot_001";
        std::vector<CameraSnapshot> snapshots_;
        int selectedIndex_ = -1;
    };
}

#endif // _DEBUG

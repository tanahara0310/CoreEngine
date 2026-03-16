#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"
#include "Math/Vector/Vector3.h"

#include <string>

namespace CoreEngine
{
    class GameObject;

    /// @brief オブジェクト追従/注視カメラを編集するモジュール
    class CameraFollowEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "追従/注視"; }

        /// @brief 毎フレーム更新（追従/注視の適用）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        /// @brief 追従先名から対象オブジェクトを検索
        GameObject* FindTargetObject(const CameraEditorContext& context) const;

        /// @brief Releaseカメラへ追従/注視を適用
        void ApplyToReleaseCamera(const CameraEditorContext& context, const Vector3& targetPosition) const;

        /// @brief Debugカメラへ追従/注視を適用
        void ApplyToDebugCamera(const CameraEditorContext& context, const Vector3& targetPosition) const;

        /// @brief 線形補間（Vector3）
        static Vector3 LerpVector3(const Vector3& from, const Vector3& to, float t);

    private:
        bool enabled_ = false;
        bool followEnabled_ = true;
        bool lookAtEnabled_ = true;

        std::string targetObjectName_;
        Vector3 followOffset_ = { 0.0f, 2.0f, -8.0f };
        Vector3 lookAtOffset_ = { 0.0f, 1.0f, 0.0f };
        float followSmoothing_ = 1.0f;

        std::string statusMessage_;
    };
}

#endif // _DEBUG

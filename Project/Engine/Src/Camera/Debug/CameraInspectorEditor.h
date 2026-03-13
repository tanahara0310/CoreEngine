#pragma once

#ifdef _DEBUG

#include "Math/MathCore.h"

namespace CoreEngine
{
    class ICamera;
    class DebugCamera;
    class Camera;

    /// @brief カメラインスペクターUI（3D/2Dの基本情報とカメラ種別ごとの詳細設定）
    class CameraInspectorEditor {
    public:
        /// @brief カメラ基本情報を描画
        /// @param camera カメラ
        void DrawCameraBasicInfo(ICamera* camera);

        /// @brief DebugCamera用インスペクターを描画
        /// @param debugCamera DebugCamera
        void DrawDebugCameraInspector(DebugCamera* debugCamera);

        /// @brief ReleaseCamera用インスペクターを描画
        /// @param camera ReleaseCamera
        void DrawReleaseCameraInspector(Camera* camera);

    private:
        /// @brief ReleaseCameraの方向ベクトル情報を描画
        void DrawCameraVectorInfo(Camera* camera);

    private:
        bool showAdvancedSettings_ = false;
        bool releaseCameraLookAtMode_ = false;
        Vector3 releaseCameraLookAtTarget_ = { 0.0f, 0.0f, 0.0f };
    };
}

#endif // _DEBUG

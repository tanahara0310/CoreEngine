#include "pch.h"
#include "DebugCameraCVars.h"

#include "Camera/Camera.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Utility/CVar/CVar.h"

#include <numbers>

namespace
{
    using namespace CoreEngine;

    /// 実体（コントローラ・カメラ）を鏡写しにするだけなので、自動生成 UI には出さない。
    /// 出してしまうと毎フレームのミラーに即上書きされ、触れるのに効かない UI になる
    constexpr CVarFlags kMirrorFlags = CVarFlags::NoUI;

    // ===== 操作設定（OrbitFlyController::Settings と既定値を一致させること） =====
    CVar<float> cvRotationSensitivity{
        "d.SceneCamera.RotationSensitivity", 0.003f,
        "マウス回転感度", CVarRange{ 0.0001f, 0.05f }, kMirrorFlags };
    CVar<float> cvPanSensitivity{
        "d.SceneCamera.PanSensitivity", 0.05f,
        "パン感度 [ワールド単位/ピクセル]", CVarRange{ 0.001f, 5.0f }, kMirrorFlags };
    CVar<float> cvZoomSensitivity{
        "d.SceneCamera.ZoomSensitivity", 2.0f,
        "ズーム 1 ノッチあたりのドリー移動量 [m]", CVarRange{ 0.01f, 100.0f }, kMirrorFlags };
    CVar<float> cvMinDistance{
        "d.SceneCamera.MinDistance", 0.1f,
        "注視点までの最小距離 [m]", CVarRange{ 0.01f, 100.0f }, kMirrorFlags };
    CVar<float> cvMaxDistance{
        "d.SceneCamera.MaxDistance", 10000.0f,
        "注視点までの最大距離 [m]", CVarRange{ 1.0f, 100000.0f }, kMirrorFlags };
    CVar<bool> cvInvertY{
        "d.SceneCamera.InvertY", false,
        "Y 軸反転", {}, kMirrorFlags };
    CVar<bool> cvSmoothMovement{
        "d.SceneCamera.SmoothMovement", true,
        "スムーズ移動", {}, kMirrorFlags };
    CVar<float> cvSmoothingFactor{
        "d.SceneCamera.SmoothingFactor", 0.2f,
        "スムージング係数", CVarRange{ 0.01f, 1.0f }, kMirrorFlags };
    CVar<float> cvFlySpeed{
        "d.SceneCamera.FlySpeed", 10.0f,
        "WASD 自由移動の基本速度 [m/s]", CVarRange{ 0.1f, 500.0f }, kMirrorFlags };
    CVar<float> cvFlySpeedBoost{
        "d.SceneCamera.FlySpeedBoost", 4.0f,
        "Shift 押下時の速度倍率", CVarRange{ 1.0f, 20.0f }, kMirrorFlags };
    CVar<float> cvMaxHorizontalExtent{
        "d.SceneCamera.MaxHorizontalExtent", 5000.0f,
        "注視点の水平(XZ)移動限界 [m]", CVarRange{ 100.0f, 100000.0f }, kMirrorFlags };
    CVar<float> cvMinHeight{
        "d.SceneCamera.MinHeight", -100.0f,
        "注視点の最低高度 [m]", CVarRange{ -10000.0f, 0.0f }, kMirrorFlags };
    CVar<float> cvMaxHeight{
        "d.SceneCamera.MaxHeight", 10000.0f,
        "注視点の最高高度 [m]", CVarRange{ 0.0f, 100000.0f }, kMirrorFlags };

    // ===== 軌道状態（OrbitFlyController::OrbitState と既定値を一致させること） =====
    // 前回終了時の視点から再開するための「実行時状態だが保存する」項目。
    // 初期視点に戻したい場合は CVars.json から d.SceneCamera.* を削除する
    CVar<Vector3> cvTarget{
        "d.SceneCamera.Target", { 0.0f, 0.0f, 0.0f },
        "注視点（回転中心）のワールド座標", {}, kMirrorFlags };
    CVar<float> cvDistance{
        "d.SceneCamera.Distance", 20.0f,
        "注視点までの距離 [m]", {}, kMirrorFlags };
    CVar<float> cvPitch{
        "d.SceneCamera.Pitch", 0.25f,
        "ピッチ [rad]", {}, kMirrorFlags };
    CVar<float> cvYaw{
        "d.SceneCamera.Yaw", std::numbers::pi_v<float>,
        "ヨー [rad]", {}, kMirrorFlags };

    // ===== 投影パラメータ（CameraParameters と既定値を一致させること） =====
    // aspectRatio はウィンドウサイズから毎フレーム導出される実行時値のため CVar 化しない
    CVar<float> cvFov{
        "d.SceneCamera.Fov", 0.45f,
        "視野角 [rad]", CVarRange{ 0.01f, 3.0f }, kMirrorFlags };
    CVar<float> cvNearClip{
        "d.SceneCamera.NearClip", 0.1f,
        "ニアクリップ [m]", CVarRange{ 0.001f, 100.0f }, kMirrorFlags };
    CVar<float> cvFarClip{
        "d.SceneCamera.FarClip", 1000.0f,
        "ファークリップ [m]", CVarRange{ 10.0f, 100000.0f }, kMirrorFlags };
}

namespace CoreEngine
{
    void DebugCameraCVars::RestoreTo(Camera& camera, OrbitFlyController& controller)
    {
        // コントローラ・カメラの生成直後（＝構造体のコード既定値そのまま）に呼ばれる前提のため、
        // AtmosphereLights と違い IsModified() で絞らず全項目を流し込んでよい。
        // CVar の既定値は各構造体の既定値と一致させてあるので、未保存の項目は同じ値が入る
        auto s = controller.GetSettings();
        s.rotationSensitivity  = cvRotationSensitivity.Get();
        s.panSensitivity       = cvPanSensitivity.Get();
        s.zoomSensitivity      = cvZoomSensitivity.Get();
        s.minDistance          = cvMinDistance.Get();
        s.maxDistance          = cvMaxDistance.Get();
        s.invertY              = cvInvertY.Get();
        s.smoothMovement       = cvSmoothMovement.Get();
        s.smoothingFactor      = cvSmoothingFactor.Get();
        s.flySpeed             = cvFlySpeed.Get();
        s.flySpeedBoost        = cvFlySpeedBoost.Get();
        s.maxHorizontalExtent  = cvMaxHorizontalExtent.Get();
        s.minHeight            = cvMinHeight.Get();
        s.maxHeight            = cvMaxHeight.Get();
        controller.SetSettings(s);

        // 軌道状態（SetState がクランプとスムーズ値の同期までまとめて行う）。
        // 移動範囲のクランプが効くよう、設定の適用より後に行うこと
        auto state = controller.GetState();
        state.target   = cvTarget.Get();
        state.distance = cvDistance.Get();
        state.pitch    = cvPitch.Get();
        state.yaw      = cvYaw.Get();
        controller.SetState(state);

        CameraParameters params = camera.GetParameters();
        params.fov      = cvFov.Get();
        params.nearClip = cvNearClip.Get();
        params.farClip  = cvFarClip.Get();
        camera.SetParameters(params);

        // 復元直後に姿勢と行列を反映しておく（次フレームの Update を待たない）
        controller.ApplyTo(camera);
        camera.UpdateMatrix();
    }

    void DebugCameraCVars::MirrorFrom(const Camera& camera, const OrbitFlyController& controller)
    {
        // CVar::Set は値が実際に変わったときだけ変更通番を進めるため、毎フレーム呼んでよい
        const auto& s = controller.GetSettings();
        cvRotationSensitivity.Set(s.rotationSensitivity);
        cvPanSensitivity.Set(s.panSensitivity);
        cvZoomSensitivity.Set(s.zoomSensitivity);
        cvMinDistance.Set(s.minDistance);
        cvMaxDistance.Set(s.maxDistance);
        cvInvertY.Set(s.invertY);
        cvSmoothMovement.Set(s.smoothMovement);
        cvSmoothingFactor.Set(s.smoothingFactor);
        cvFlySpeed.Set(s.flySpeed);
        cvFlySpeedBoost.Set(s.flySpeedBoost);
        cvMaxHorizontalExtent.Set(s.maxHorizontalExtent);
        cvMinHeight.Set(s.minHeight);
        cvMaxHeight.Set(s.maxHeight);

        const auto& state = controller.GetState();
        cvTarget.Set(state.target);
        cvDistance.Set(state.distance);
        cvPitch.Set(state.pitch);
        cvYaw.Set(state.yaw);

        const CameraParameters params = camera.GetParameters();
        cvFov.Set(params.fov);
        cvNearClip.Set(params.nearClip);
        cvFarClip.Set(params.farClip);
    }
}

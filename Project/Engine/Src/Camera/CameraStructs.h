#pragma once
#include <Math/MathCore.h>
#include <string>

/// @brief カメラのデータ型（投影パラメータ・GPU 構造体・スナップショット）

namespace CoreEngine
{

/// @brief カメラのタイプ（アクティブカメラを 3D / 2D で別々に持つための区分）
enum class CameraType {
    Camera3D,  // 3D用カメラ（透視投影）
    Camera2D   // 2D用カメラ（正射影）
};

/// @brief エンジンが既定で用意するカメラの名前
/// @details 役割は API 名（SceneCamera / GameCamera）で表す。ここは登録キーの文字列で、
///          コード各所へマジックストリングが散らばるのを防ぐためだけに置いている。
namespace CameraNames {
    /// @brief エディタ視点カメラ（Blender 風の軌道操作つき）
    inline constexpr const char* Scene = "Debug";
    /// @brief ゲーム視点カメラ（シーンが構図を決める）
    inline constexpr const char* Game = "Release";
    /// @brief 2D（正射影）カメラ
    inline constexpr const char* Camera2D = "Camera2D";
}

/// @brief カメラのGPU用構造体
struct CameraForGPU {
    Vector3 worldPosition; // ワールド座標
};

/// @brief 投影方式
enum class CameraProjectionType {
    Perspective,   ///< 透視投影（3D）
    Orthographic   ///< 正射影（2D。画面中央が原点、Y軸上が正）
};

/// @brief カメラパラメータ構造体
/// @details 投影方式もここに含める。カメラの具象クラスを 1 つに保つため、
///          「3Dカメラ / 2Dカメラ」はクラスの違いではなくこのパラメータの違いで表す。
struct CameraParameters {
    CameraProjectionType projectionType = CameraProjectionType::Perspective;

    // 透視投影パラメータ
    float fov = 0.45f;                  // 視野角（ラジアン）
    float nearClip = 0.1f;              // ニアクリップ
    float farClip = 1000.0f;            // ファークリップ

    // アスペクト比（通常は自動計算されるが、カスタム設定も可能）
    float aspectRatio = 0.0f;           // 0.0f = 自動計算

    /// @brief デフォルトパラメータを取得
    static CameraParameters Default() {
        return CameraParameters{};
    }

    /// @brief 2D（正射影）用の既定パラメータを取得
    static CameraParameters Orthographic2D() {
        CameraParameters params;
        params.projectionType = CameraProjectionType::Orthographic;
        params.nearClip = 0.0f;
        params.farClip = 100.0f;
        return params;
    }

    /// @brief パラメータをデフォルトにリセット
    void Reset() {
        const CameraProjectionType keepType = projectionType;
        *this = Default();
        projectionType = keepType;
    }

    /// @brief 視野角を度数法で取得
    /// @return 視野角（度）
    float GetFovDegrees() const {
        return fov * 180.0f / 3.14159265359f;
    }

    /// @brief 視野角を度数法で設定
    /// @param degrees 視野角（度）
    void SetFovDegrees(float degrees) {
        fov = degrees * 3.14159265359f / 180.0f;
    }

    /// @brief このパラメータが表すカメラタイプ
    CameraType GetCameraType() const {
        return (projectionType == CameraProjectionType::Orthographic)
            ? CameraType::Camera2D
            : CameraType::Camera3D;
    }
};

/// @brief カメラスナップショット（プリセット・キーフレーム保存用）
/// @details 以前は Camera 用と DebugCamera 用のフィールドを両方持ち isDebugCamera で
///          分岐する手書きタグ付きユニオンだったが、カメラが 1 種類になったため
///          「Transform + 投影パラメータ」だけになった。
///          軌道操作（注視点・距離・ピッチ・ヨー）はカメラの状態ではなく
///          OrbitFlyController の内部状態なので、こことは別に保存する。
struct CameraSnapshot {
    std::string name;                   // プリセット名

    Vector3 position = { 0.0f, 0.0f, -10.0f };
    Vector3 rotation = { 0.0f, 0.0f, 0.0f };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };

    CameraParameters parameters;

    /// @brief デフォルトスナップショットを作成
    static CameraSnapshot Default(const std::string& name = "Default") {
        CameraSnapshot snapshot;
        snapshot.name = name;
        return snapshot;
    }
};

}

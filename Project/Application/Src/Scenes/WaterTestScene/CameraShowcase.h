#pragma once

#include "Math/Vector/Vector3.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace CoreEngine {
    class EngineSystem;
    class FadeEffect;
}

/// @brief 起動時のリリースカメラを「ワンカット見せる → 黒へフェード → 暗転中に構図を差し替える」で巡回させる演出
/// @details 1 つの構図に固定すると、水面すれすれの構図では大気散乱の白いもやが画面の半分を占めるなど
///          どの高さを選んでも一長一短になる。複数のカットを回すことで、それぞれの構図の良いところ
///          （浅瀬のコースティクス／外洋のうねり／俯瞰の島々）だけを見せる。
///          カメラの切り替えは必ず完全暗転中に行うため、視点がワープする瞬間は画に出ない。
///          演出はリリース（ゲーム視点）カメラのためのものなので、デバッグ（エディタ）視点へ
///          切り替えている間はフェードを畳んで進行も止める。リリースカメラへ戻すと続きから再開する。
class CameraShowcase {
public:
    /// @brief 1 カット分の構図
    struct Shot {
        CoreEngine::Vector3 translate{};      ///< ワールド座標
        CoreEngine::Vector3 rotate{};         ///< オイラー角 [rad]（エンジン規約は Rx*Ry*Rz）
        float fovDegrees = 50.0f;             ///< 垂直画角 [度]
        float farClip = 20000.0f;             ///< ファークリップ [m]
    };

    /// @brief 構図をカメラへ適用する処理（シーン側が BaseScene の API を呼ぶ）
    using ApplyShotFunc = std::function<void(const Shot&)>;

    /// @brief リリース（ゲーム視点）カメラで覗いているかを返す処理
    /// @details カメラの役割を持つのは CameraManager（BaseScene が所有）なので、
    ///          この演出クラスからは直接引かずにシーン側から渡してもらう。
    using IsReleaseCameraActiveFunc = std::function<bool()>;

    /// @param engine    FadeEffect（ポストエフェクト）を引くために使う
    /// @param shots     巡回する構図（空なら何もしない）
    /// @param applyShot 構図の適用処理
    /// @param isReleaseCameraActive リリースカメラで覗いているかの判定（未設定なら常に有効として扱う）
    void Initialize(CoreEngine::EngineSystem* engine, std::vector<Shot> shots, ApplyShotFunc applyShot,
        IsReleaseCameraActiveFunc isReleaseCameraActive = {});

    /// @brief 毎フレーム更新（フェード進行とカット切り替え）
    void Update(float deltaTime);

    /// @brief 後始末。フェードを解除して黒画面を残さない
    /// @param keepFade シーン遷移のフェード中など、他者がフェードを使っているときは true
    void Shutdown(bool keepFade);

private:
    /// @brief 演出の進行段階
    enum class Phase {
        FadeIn,   ///< 黒 → 画（暗転中に構図の切り替えを終えている）
        Hold,     ///< カットを見せている
        FadeOut,  ///< 画 → 黒
        Black,    ///< 完全暗転を保つ（ここで構図を差し替える）
    };

    void ApplyCurrentShot();
    void ApplyFadeAlpha(float alpha);

    /// @brief 今の phase_ / timer_ が示すフェードの濃さ
    /// @details 中断から復帰したときに、止めた時点の濃さへ戻すために使う
    float CurrentPhaseAlpha() const;

    /// @brief リリースカメラで覗いているか（判定が未設定なら true）
    bool IsReleaseCameraActive() const;

    CoreEngine::EngineSystem* engine_ = nullptr;
    CoreEngine::FadeEffect* fadeEffect_ = nullptr;

    std::vector<Shot> shots_{};
    ApplyShotFunc applyShot_{};
    IsReleaseCameraActiveFunc isReleaseCameraActive_{};

    std::size_t currentIndex_ = 0;
    Phase phase_ = Phase::FadeIn;
    float timer_ = 0.0f;

    /// 直前フレームで演出が有効だったか（CVar で切ったときにフェードを畳むため）
    bool wasEnabled_ = true;

    /// デバッグカメラへ切り替えたことで演出を止めているか（phase_ / timer_ は保持する）
    bool suspended_ = false;
};

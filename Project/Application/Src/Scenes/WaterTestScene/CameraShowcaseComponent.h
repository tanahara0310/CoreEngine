#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace CoreEngine {
    class FadeEffect;
}

/// @brief ゲーム視点のカメラに「1 カット見せる → 黒へフェード → 暗転中に構図を差し替える」を繰り返させるコンポーネント
/// @details カットの一覧と、構図をカメラへ当てる処理はシーンが `Configure()` で渡す。
///          カメラの切り替えは完全に暗転している間に行う。
///          エディタのカメラで覗いている間は、フェードを畳んで進行を止め、ゲーム視点へ戻すと続きから再開する。
class CameraShowcaseComponent : public CoreEngine::IComponent {
public:
    /// @brief 1 カット分の構図
    struct Shot {
        CoreEngine::Vector3 translate{};      ///< ワールド座標
        CoreEngine::Vector3 rotate{};         ///< オイラー角 [rad]（エンジン規約は Rx*Ry*Rz）
        float fovDegrees = 50.0f;             ///< 垂直画角 [度]
        float farClip = 20000.0f;             ///< ファークリップ [m]
    };

    /// @brief 構図をカメラへ当てる処理
    using ApplyShotFunc = std::function<void(const Shot&)>;

    /// @brief ゲーム視点のカメラで覗いているかを返す処理
    using IsGameCameraActiveFunc = std::function<bool()>;

    const char* GetTypeName() const override { return "CameraShowcase"; }

    REFLECT_BEGIN(CameraShowcaseComponent, "カット巡回")
        REFLECT_PROPERTY(cycling_, "巡回する",
            p.tooltip = "切るとフェードを外し、今のカットのまま止める")
        REFLECT_PROPERTY(holdSeconds_, "1 カットの時間 [秒]", p.range = Range(0.5f, 60.0f, 0.1f))
        REFLECT_PROPERTY(fadeSeconds_, "フェードの時間 [秒]", p.range = Range(0.1f, 10.0f, 0.05f))
        REFLECT_PROPERTY(blackSeconds_, "暗転を保つ時間 [秒]", p.range = Range(0.0f, 5.0f, 0.05f))
        REFLECT_PROPERTY(startIndex_, "最初のカット",
            p.range = Range(0.0f, 32.0f),
            p.tooltip = "再生を始めたときに見せるカットの番号（範囲外は先頭へ丸める）")
        REFLECT_PROPERTY(overrideShot_, "構図を上書きする",
            p.tooltip = "カットの一覧の代わりに、下の構図だけを見せる")
        REFLECT_PROPERTY(overridePosition_, "上書き構図の位置 [m]", p.range = Speed(0.1f))
        REFLECT_PROPERTY(overrideRotation_, "上書き構図の回転 [度]",
            p.range = Speed(0.1f), p.displayScale = kDegreesPerRadian)
        REFLECT_PROPERTY(overrideFovDegrees_, "上書き構図の画角 [度]", p.range = Range(10.0f, 120.0f, 0.1f))
        REFLECT_PROPERTY(overrideFarClip_, "上書き構図のファークリップ [m]", p.range = Range(100.0f, 200000.0f, 10.0f))
    REFLECT_END()

    /// @brief カットの一覧とカメラへの当て方を渡し、最初のカットを当てる
    /// @param shots 巡回する構図（空なら何もしない）
    /// @param applyShot 構図の当て方
    /// @param isGameCameraActive ゲーム視点のカメラで覗いているかの判定（未設定なら常に覗いているとみなす）
    void Configure(std::vector<Shot> shots, ApplyShotFunc applyShot, IsGameCameraActiveFunc isGameCameraActive = {});

    /// @brief 最初のカットから黒で始める
    void Start() override;

    /// @brief フェードを進め、暗転しきったら次のカットへ差し替える
    void Update() override;

    /// @brief フェードを外す（シーン遷移のフェード中は触らない）
    void OnDestroy() override;

    /// @brief 値が変わったら、上書き構図と巡回の切り替えをすぐ反映する
    void OnPropertyChanged(const CoreEngine::Reflection::PropertyDescriptor& property) override;

private:
    /// @brief 進行段階
    enum class Phase {
        FadeIn,   ///< 黒 → 画（暗転中に構図の切り替えを終えている）
        Hold,     ///< カットを見せている
        FadeOut,  ///< 画 → 黒
        Black,    ///< 完全暗転を保つ（ここで構図を差し替える）
    };

    /// @brief 今のカット（上書きが有効なら上書き構図）をカメラへ当てる
    void ApplyCurrentShot();

    /// @brief フェードの濃さを当てる（ほぼ透明ならフェードのパスを切る）
    void ApplyFadeAlpha(float alpha);

    /// @brief 今の段階と経過時間が示すフェードの濃さ
    float CurrentPhaseAlpha() const;

    /// @brief ゲーム視点のカメラで覗いているか（判定が未設定なら true）
    bool IsGameCameraActive() const;

    /// @brief 最初のカットの番号（カットの数で丸める）
    std::size_t ClampedStartIndex() const;

    // ===== 設定 =====
    bool cycling_ = true;
    float holdSeconds_ = 6.0f;
    float fadeSeconds_ = 1.2f;
    float blackSeconds_ = 0.4f;
    int startIndex_ = 0;
    bool overrideShot_ = false;
    CoreEngine::Vector3 overridePosition_{ 10.0f, 35.0f, 190.0f };
    CoreEngine::Vector3 overrideRotation_{ 0.1155f, -3.1216f, 0.0f };
    float overrideFovDegrees_ = 55.0f;
    float overrideFarClip_ = 20000.0f;

    // ===== 実行時の状態 =====
    CoreEngine::FadeEffect* fadeEffect_ = nullptr;
    std::vector<Shot> shots_{};
    ApplyShotFunc applyShot_{};
    IsGameCameraActiveFunc isGameCameraActive_{};

    std::size_t currentIndex_ = 0;
    Phase phase_ = Phase::FadeIn;
    float timer_ = 0.0f;
    bool started_ = false;
    bool wasCycling_ = true;   ///< 直前のフレームで巡回していたか
    bool suspended_ = false;   ///< エディタのカメラで覗いているため止めているか
};

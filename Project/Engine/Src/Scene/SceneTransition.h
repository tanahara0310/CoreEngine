#pragma once

#include <memory>

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class PostEffectManager;
    class FadeEffect;
    class LoadingScreenEffect;
    class ToneMapping;
    class AudioSystem;
}

namespace CoreEngine
{
/// @brief シーントランジション管理クラス
/// @details シーン遷移時のフェードイン・フェードアウトなどの演出を管理（ポストエフェクトベース）
class SceneTransition {
public:
    /// @brief トランジションタイプ
    enum class TransitionType {
        None,       // トランジションなし（即座に切り替え）
        Fade,       // フェード
        Loading,    // ローディング画面（デフォルト）
        Slide,      // スライド（未実装）
        Dissolve    // ディゾルブ（未実装）
    };

    /// @brief トランジションフェーズ
    enum class TransitionPhase {
        Idle,       // 待機中（トランジション無し）
        FadeOut,    // フェードアウト中
        Loading,    // ローディング画面表示中
        Changing,   // シーン切り替え準備完了
        FadeIn      // フェードイン中
    };

public:
    SceneTransition() = default;
    ~SceneTransition() = default;

    /// @brief 初期化
    /// @param engine エンジンシステムへのポインタ
    void Initialize(EngineSystem* engine);

    /// @brief 更新処理
    /// @param deltaTime デルタタイム（秒）
    void Update(float deltaTime);

    /// @brief トランジション開始（SceneManagerから呼ばれる）
    /// @param type トランジションタイプ
    /// @param duration トランジションの持続時間（秒）
    void StartTransition(TransitionType type, float duration);

    /// @brief シーン切り替え準備が完了したか確認
    /// @return true: シーン切り替え可能, false: まだフェードアウト中
    bool IsReadyToChangeScene() const;

    /// @brief シーン切り替え完了通知（フェードイン開始）
    void OnSceneChanged();

    /// @brief トランジション中か確認
    /// @return true: トランジション中, false: 待機中
    bool IsTransitioning() const;

    /// @brief トランジションがシーンの更新をブロックするか
    /// @return true: ブロック中, false: 更新可能
    bool IsBlocking() const;

    /// @brief 現在のフェーズを取得
    /// @return 現在のトランジションフェーズ
    TransitionPhase GetCurrentPhase() const { return phase_; }

    /// @brief トランジションをスキップ（デバッグ用）
    void SkipTransition();

    /// @brief シーン読み込みの進捗を設定する（SceneManager が毎フレーム呼ぶ）
    /// @param progress 進捗（0.0〜1.0）
    void SetLoadProgress(float progress);

private:
    /// @brief フェードアルファ値を計算
    /// @return アルファ値（0.0 = 透明, 1.0 = 不透明）
    float CalculateFadeAlpha() const;

    /// @brief ポストエフェクトにフェード値を適用
    void ApplyFadeToPostEffect();

    /// @brief ローディング画面の表示強度を計算
    /// @return 表示強度（0.0 = 非表示, 1.0 = 完全表示）
    float CalculateLoadingAlpha() const;

    /// @brief 進捗ゲージの表示強度を計算
    /// @return 表示強度（0.0 = 非表示, 1.0 = 完全表示）
    float CalculateGaugeAlpha() const;

    /// @brief ローディング画面に表示強度を適用
    void ApplyLoadingScreen();

    /// @brief 暗転中は自動露出の順応を止める
    /// @details 自動露出は「見えている絵」への順応。暗転しきっている間はシーンが
    ///          解放されていて SceneColor が真っ黒なので、そこへ順応させると
    ///          順応輝度が 0 まで落ち、次のシーンが白飛びした状態で現れる。
    void ApplyExposureHold();

    /// @brief BGM バスのダッキングをフェードへ同期させる
    /// @details AudioBus::BGM のダッキング係数だけを動かすので、オプション画面が
    ///          設定したバス音量（SetBusVolume）は壊さない。BGM を個別に登録する
    ///          必要も無い（BGM バスに出ている音は全部まとめて絞られる）。
    void ApplyBGMVolume();

private:
EngineSystem* engine_ = nullptr;
PostEffectManager* postEffectManager_ = nullptr;
FadeEffect* fadeEffect_ = nullptr;
LoadingScreenEffect* loadingScreenEffect_ = nullptr;
ToneMapping* toneMapping_ = nullptr;
AudioSystem* audioSystem_ = nullptr;

TransitionPhase phase_ = TransitionPhase::Idle;
    TransitionType type_ = TransitionType::None;

    float timer_ = 0.0f;        // 現在のタイマー
    float duration_ = 1.0f;     // トランジション時間（秒）
    
    // フェードアウト完了後の待機フレーム数（完全暗転を確実にするため）
    static constexpr int kWaitFramesAfterFadeOut = 3;

    // ローディング画面の表示強度が切り替わる時間（秒）
    static constexpr float kLoadingFadeSeconds = 0.25f;

    // 進捗ゲージが現れるまでの時間（秒）
    static constexpr float kGaugeFadeSeconds = 0.3f;

    // このフェードアルファ以上を「画面が見えていない」とみなす（自動露出の凍結境界）
    static constexpr float kExposureHoldAlpha = 0.98f;

    float loadingElapsed_ = 0.0f;   // ローディング画面を表示している時間
    float loadProgress_ = 0.0f;     // シーン読み込みの進捗
    int waitFrameCounter_ = 0;
};
}

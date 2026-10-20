#pragma once

#include <cstdint>
#include <span>
#include <string>

// 前方宣言
namespace CoreEngine {
    class EngineSystem;
    class PostEffectManager;
    class FadeEffect;
    class ILoadingScreenEffect;
    class ToneMapping;
    class AudioSystem;
    class ProjectPostEffect;
    class UIOverlay;
}

namespace CoreEngine
{
/// @brief シーントランジション管理クラス
/// @details シーン遷移時のフェードイン・フェードアウトなどの演出を管理（ポストエフェクトベース）。
///          フェーズごとの答え（フェードの濃さ・ローディング画面の濃さ・BGM の絞り・シーンを止めるか）は
///          フェーズの状態クラス（Phase）が持ち、遷移の種類は通るフェーズの順番（Route）で表す。
class SceneTransition {
public:
    /// @brief トランジションタイプ
    enum class TransitionType {
        None,       // トランジションなし（即座に切り替え）
        Fade,       // フェード
        Loading,    // ローディング画面（デフォルト）
        Effect      // プロジェクトのポストエフェクト（Project.json の postEffects）で覆う
    };

    /// @brief 遷移の指定
    struct Settings {
        TransitionType type = TransitionType::Loading;
        std::string effect;       ///< Effect のときに使うプロジェクトのポストエフェクトの名前
        float outSeconds = 0.4f;  ///< 覆うまでの秒数
        float inSeconds = 0.4f;   ///< 明けるまでの秒数
        bool skipOut = false;     ///< 覆う段を飛ばし、覆われた状態から明ける
        bool hideUI = false;      ///< 遷移の間は UI を描かない
        bool duckBgm = true;      ///< 覆うのに合わせて BGM を絞る
    };

    /// @brief トランジションフェーズ
    /// @details Loading 系の並びは FadeOut → Loading → Hold → FadeIn。
    ///          Changing は「ローディング画面を出さない遷移」（Fade など）が
    ///          シーン構築を回すための段で、Loading 系では通らない。
    enum class TransitionPhase {
        Idle,       // 待機中（トランジション無し）
        FadeOut,    // フェードアウト中
        Loading,    // ローディング画面表示中。この間にシーンを構築する
        Changing,   // シーン切り替え準備完了（ローディング画面を出さない遷移用）
        Hold,       // 構築完了。ローディング画面の最低表示時間を満たすまで待つ
        FadeIn      // フェードイン中
    };

    /// @brief 遷移の種類ごとに通るフェーズの順番（最後まで進むと Idle へ戻る）
    struct Route {
        std::span<const TransitionPhase> phases;
        bool usesFade = false;           ///< フェードのエフェクトを使うか
        bool showsLoadingScreen = false; ///< ローディング画面を出すか
        bool usesEffect = false;         ///< プロジェクトのポストエフェクトで覆うか
    };

    /// @brief フェーズの間で受け渡す値（各フェーズはこれを読み書きする）
    struct Context {
        float duration = 1.0f;       ///< フェードの長さ（秒）
        float timer = 0.0f;          ///< 今のフェーズに入ってからの時間（秒）
        float loadingElapsed = 0.0f; ///< ローディング画面を表示している時間（秒）
        float loadProgress = 0.0f;   ///< シーン読み込みの進捗（0.0〜1.0）
        float arrivedElapsed = 0.0f; ///< 表示進捗が 1.0 に届いてから待った時間（秒）
        int waitFrameCounter = 0;    ///< フェードアウトしきってから待ったフレーム数
        bool showsLoadingScreen = false;

        /// @brief ローディング画面へ見せる進捗
        /// @details 「読み込みの進み」と「最低表示時間の進み」の遅い方に合わせる。
        ///          どちらも満たされて初めて 1.0 になるので、100% に届く瞬間が
        ///          必ずフェードインの直前になる。素の loadProgress をそのまま出すと、
        ///          読み込みが速いときに 100% へ着いてから最低表示時間ぶん待たされ、
        ///          遅いときは 100% の絵を見せる前に切り替わる。
        float DisplayProgress() const;
    };

    /// @brief フェーズの状態クラスの基底（SceneTransition.cpp に置く）
    class Phase;

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

    /// @brief 指定どおりにトランジションを始める（SceneManagerから呼ばれる）
    /// @note Effect でエフェクトが見つからなければ、警告を出してフェードで代わりにする
    void StartTransition(const Settings& settings);

    /// @brief シーン切り替え準備が完了したか確認
    /// @return true: シーン切り替え可能, false: まだフェードアウト中
    bool IsReadyToChangeScene() const;

    /// @brief シーン切り替え完了通知（フェードイン開始）
    void OnSceneChanged();

    /// @brief トランジション中か確認
    /// @return true: トランジション中, false: 待機中
    bool IsTransitioning() const { return phase_ != TransitionPhase::Idle; }

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

    /// @brief ローディング画面の見た目を差し替える
    /// @param effectName PostEffectNames のうち ILoadingScreenEffect を実装したエフェクト名
    /// @return 差し替えられたら true。名前が無い・実装していない場合は false（元のまま）
    /// @details フェーズの進み方には影響しない。ゲーム側の初期化で 1 回呼ぶだけでよい。
    ///          既定はエンジン汎用の PostEffectNames::LoadingScreen。
    bool SetLoadingScreen(const char* effectName);

private:
    /// @brief フェーズを切り替え、入口の処理と外への反映を行う（遷移はここだけ）
    void ChangePhase(TransitionPhase next);

    /// @brief 遷移に使うポストエフェクトへ、有効かどうかと進み具合を渡す
    /// @param coverage 覆っている度合い（0.0 = 素通し, 1.0 = 覆いきり）
    void ApplyEffect(float coverage);

    /// @brief 経路の次のフェーズへ進む（最後まで進んだら Idle）
    void AdvanceRoute();

    /// @brief 今のフェーズの答えを、フェード・ローディング画面・自動露出・BGM へ流す
    void Apply();

    /// @brief 進捗ゲージの表示強度（0.0 = 非表示, 1.0 = 完全表示）
    /// @param loadingAlpha ローディング画面の表示強度
    float CalculateGaugeAlpha(float loadingAlpha) const;

private:
    EngineSystem* engine_ = nullptr;
    PostEffectManager* postEffectManager_ = nullptr;
    FadeEffect* fadeEffect_ = nullptr;
    ILoadingScreenEffect* loadingScreenEffect_ = nullptr;
    ToneMapping* toneMapping_ = nullptr;
    AudioSystem* audioSystem_ = nullptr;
    UIOverlay* uiOverlay_ = nullptr;

    /// 今の遷移の指定
    Settings settings_{};
    /// 今の遷移に使っているポストエフェクト（Effect のときだけ）
    ProjectPostEffect* transitionEffect_ = nullptr;
    /// 遷移に使っているポストエフェクトを有効にしたか
    bool transitionEffectEnabled_ = false;

    TransitionPhase phase_ = TransitionPhase::Idle;
    const Phase* current_ = nullptr;
    Route route_{};
    size_t routeIndex_ = 0;
    Context context_{};
};
}

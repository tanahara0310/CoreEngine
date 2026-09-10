#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"

#include <array>
#include <vector>

namespace CoreEngine
{
    class UIImage;
    class UIText;
}

namespace GameComponents
{
    class TrainMovementComponent;

    /// @brief トロッコの速さを km/h のオドメーターで見せる HUD。
    /// @details 桁は 3 桁＋小数 1 桁の `000.0` 固定で、先頭の 0 も出す（オドメーターの見た目）。
    ///          `TrainMovementComponent` の速度はマス/秒なので、`Game.SpeedGauge.MetersPerCell`
    ///          （1 マスの実距離 [m]）を掛けて km/h に直している。
    /// @note 桁が変わるときは、前の数字が上へ送り出されて新しい数字が下から入ってくる。
    ///       UI に矩形クリップが無いため、窓で切り取るのではなく透明度で入れ替えている。
    /// @note 板・端木・蔦はスタミナゲージと同じテクスチャを使っているので、新規アセットは無い。
    ///       寸法・揺れ・明るさの考え方も `StaminaGaugeUIComponent` に揃えてある。
    ///       拡大縮小でドットがボケないよう、伸ばすのは横方向に一様な中板だけに限っている。
    /// @note 画面左上でスタミナゲージの真下に並ぶ。板の高さも幅もスタミナとほぼ同じなので、
    ///       2 段に積むと一組の掲示板に見える。位置は `Game.SpeedGauge.Position` で動かせるが、
    ///       スタミナ側を動かしたときはこちらも合わせること（自動では追従しない）。
    class SpeedGaugeUIComponent final : public CoreEngine::IComponent
    {
    public:
        explicit SpeedGaugeUIComponent(TrainMovementComponent* train = nullptr)
            : train_(train) {}

        const char* GetTypeName() const override { return "SpeedGaugeUI"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "速度計"; }
        bool DrawInspector() override;
#endif

        /// @brief 板・端木・蔦・数字を生成する（アタッチ直後）
        void Awake() override;
        /// @brief 速度を km/h に直し、桁送りと揺れを進める
        void Update() override;

        /// @brief 駅の減速に合わせて、速度計を赤く点滅させながら沈ませる
        /// @param droppedCellsPerSecond これから落ちる速度差［マス/秒］。
        ///        km/h への換算はこのコンポーネントが持っている（`MetersPerCell`）ので、
        ///        呼び出し側は列車の単位のまま渡してよい。0 以下なら何も起きない。
        /// @note 桁が下向きに転がるのは元からの挙動で、これはその上に乗せる色と沈み込み。
        ///       数字を読んでいないプレイヤーにも、画面の端の動きで減速を気づかせるためのもの。
        void PlaySlowdownFlash(float droppedCellsPerSecond);
        /// @brief 突入演出あけの登場アニメーションの進み具合
        /// @param reveal 0 = 画面外へ引っ込んだ状態 ／ 1 = 定位置。
        ///               1 を少し超える値を渡すと行き過ぎて戻る（EaseOutBack を通した値をそのまま渡す想定）
        /// @details 誰も呼ばなければ 1 のままなので、従来どおり最初から出たままになる。
        ///          駆動するのは GameEntranceFeature。
        void SetIntroReveal(float reveal);

    private:
        /// @brief 桁 1 つぶんの表示状態
        struct Digit {
            CoreEngine::UIText* current = nullptr;  ///< 今の数字
            CoreEngine::UIText* outgoing = nullptr; ///< 送り出されている前の数字
            int value = -1;         ///< 今の数字（-1 は未設定）
            int outgoingValue = -1; ///< 送り出し中の数字
            float roll = 1.0f;      ///< 0 = 送り始め／1 = 送り終わり
            float direction = 1.0f; ///< 増えたら 1（下から上へ）、減ったら -1
        };

        /// @brief 板・端木・蔦・数字を組み立てる。オーナー自身が中板になる
        void BuildParts();
        /// @brief CVar の位置・倍率を見て、各パーツを並べ直す
        void LayoutParts(float time);
        /// @brief 表示したい 4 桁を求め、変わった桁の桁送りを開始する
        void UpdateDigits(float kilometersPerHour, float deltaTime);
        /// @brief マス/秒を km/h に直す
        float CalculateKilometersPerHour() const;
        /// @brief 減速フラッシュの進み具合を 0（始まり）〜1（終わり）で返す。再生していなければ 1
        float GetSlowdownProgress() const;
        /// @brief 「▼18.4」を板の下へ置き、浮き上がりながら消えるまでを進める
        void LayoutDropLabel(
            const CoreEngine::Vector2& anchor, float panelWidth, float panelHeight, float scale);
        /// @brief トロッコが実際に進んでいるかを、水平方向の位置の変化から判定する
        /// @details 発車前（レールが規定数そろうまで）と投石中の停止を、どちらも停車として扱う。
        ///          TrainMovementComponent には走行中かを返す口が無いので、外から見て判断する。
        bool UpdateTrainMovingState();

        static constexpr std::size_t kDigitCount = 4; ///< 百・十・一・小数 1 桁

        TrainMovementComponent* train_ = nullptr;

        CoreEngine::UIImage* board_ = nullptr;    ///< 中板（オーナー自身）
        CoreEngine::UIImage* capLeft_ = nullptr;  ///< 左の端木
        CoreEngine::UIImage* capRight_ = nullptr; ///< 右の端木
        std::vector<CoreEngine::UIImage*> vines_; ///< 板の縁に絡ませる蔦
        std::array<Digit, kDigitCount> digits_{};
        CoreEngine::UIText* dot_ = nullptr;   ///< 小数点
        CoreEngine::UIText* unit_ = nullptr;  ///< "km/h"
        /// 減速したときだけ板の下へ出る「▼18.4」。落ちた量そのものを名指しする
        CoreEngine::UIText* dropLabel_ = nullptr;

        float elapsed_ = 0.0f;                    ///< 揺れ用の経過秒
        float introReveal_ = 1.0f;                ///< 登場アニメーションの進み具合（1 = 定位置）
        /// 実際に出している値。停車中は 0 で、発車すると本来の速度まで一気に振り切る
        float displayedKilometersPerHour_ = 0.0f;
        CoreEngine::Vector3 lastTrainPosition_{}; ///< 前フレームのトロッコ位置
        bool hasTrainPosition_ = false;           ///< 1 フレーム目は差分が取れない
        /// 減速フラッシュの経過秒。負のあいだは再生していない
        float slowdownElapsed_ = -1.0f;
        /// 落差から決めた強さ 0〜1。沈む深さと赤の濃さに掛かる
        float slowdownStrength_ = 0.0f;
        /// 直近の落差 [km/h]。そのまま「▼18.4」として出す
        float slowdownDropKmh_ = 0.0f;
        bool built_ = false;
    };
}

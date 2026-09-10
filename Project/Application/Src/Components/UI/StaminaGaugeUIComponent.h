#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"

#include <cstddef>
#include <vector>

namespace CoreEngine
{
    class UIImage;
}

namespace GameComponents
{
    class HungerComponent;
    class RailBuilderComponent;

    /// @brief スタミナをバナナの粒の数で見せる HUD ゲージ。
    /// @details 数字ではなく離散的な粒で表す。粒 1 つ = `Game.StaminaGauge.StaminaPerPip`
    ///          （既定 2 = レール 1 マスの基本コスト）で、5 粒ごとに房の切れ目が入るため
    ///          目盛りを兼ねる。食べた粒はその場に皮として残るので、直前に何粒持って
    ///          いかれたかが見える。
    /// @note 板・端木・粒・葉はこのコンポーネントが `Awake()` で生成する。シーン側は
    ///       `StaminaGaugeFeature` を 1 行登録するだけでよい。
    /// @note 拡大縮小でドットがボケないよう、テクスチャは基準解像度 1920x1080 の等倍で
    ///       描いてある。伸ばすのは横方向に一様な中板だけに限っている。
    class StaminaGaugeUIComponent final : public CoreEngine::IComponent
    {
    public:
        explicit StaminaGaugeUIComponent(
            HungerComponent* hunger = nullptr,
            RailBuilderComponent* builder = nullptr)
            : hunger_(hunger), builder_(builder) {}

        const char* GetTypeName() const override { return "StaminaGaugeUI"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "スタミナゲージ"; }
        bool DrawInspector() override;
#endif

        /// @brief 板・端木・粒・葉を生成する（アタッチ直後）
        void Awake() override;
        /// @brief スタミナを粒数へ落とし込み、生え／食べられのアニメーションを進める
        void Update() override;

        /// @brief 房の生え際（いま実っている一番右の粒）を返す。
        /// @param outPosition 粒の中心。基準解像度 1920x1080 の px で、UI と同じ座標系
        /// @param outSize     粒 1 つの表示サイズ [px]
        /// @return ゲージを出していない・まだ組み立てていないときは false
        /// @note 収穫したバナナをここへ飛ばして「粒になった」と見せるための着地点。
        bool TryGetFillFrontTarget(
            CoreEngine::Vector2& outPosition, CoreEngine::Vector2& outSize) const;

        /// @brief バナナがゲージへ入った瞬間の反応を鳴らす。
        /// @param staminaAmount 入ってきたスタミナ量。弾ませる粒の数に使う
        /// @note スタミナ自体は HungerComponent が加算済み。ここは見た目だけを動かす。
        ///       粒は既に実っているので、生え際から数粒を「もう一度伸び上がらせる」形で弾ませる。
        void PlayGainPop(float staminaAmount);

        /// @brief 突入演出あけの登場アニメーションの進み具合
        /// @param reveal 0 = 画面外へ引っ込んだ状態 ／ 1 = 定位置。
        ///               1 を少し超える値を渡すと行き過ぎて戻る（EaseOutBack を通した値をそのまま渡す想定）
        /// @details 誰も呼ばなければ 1 のままなので、従来どおり最初から出たままになる。
        ///          駆動するのは GameEntranceFeature。
        void SetIntroReveal(float reveal);

    private:
        /// @brief 粒 1 つぶんの表示状態
        struct Pip {
            CoreEngine::UIImage* image = nullptr;
            float grow = 1.0f;      ///< 0 = 皮だけ／1 = 実っている
            float delay = 0.0f;     ///< 生えるときのずらし秒数
            float flash = 0.0f;     ///< 切り替わった瞬間の発光
            bool  flashGain = false;///< その発光が「増えた」側か（緑）／「食べられた」側か（赤）
            bool  filled = true;    ///< 目標状態
            bool  showsFruit = true;///< 今どちらのテクスチャを差しているか
            bool  preview = false;  ///< 次の 1 マスで食べられる予定か（点滅で予告する）
        };

        void BuildParts();
        /// @brief 現在のスタミナから各粒の目標状態を決める
        void UpdateTargets();
        void UpdateAnimation(float deltaTime);
        /// @brief CVar の位置・粒数から毎フレーム配置し直す（インスペクタ調整を即反映するため）
        void ApplyLayout(float time);
        /// @brief バナナが入った反応の強さ。0 なら平常時。上へ弾んで戻る減衰波
        float GainPopWave() const;

        /// @brief 表示する粒の数（スタミナ上限 ÷ 粒あたりの量）
        std::size_t CalculatePipCount() const;

        HungerComponent* hunger_ = nullptr;
        RailBuilderComponent* builder_ = nullptr;  ///< 予告に使う。無くてもゲージは動く

        CoreEngine::UIImage* board_ = nullptr;     ///< 中板（オーナー自身。横に伸ばす）
        CoreEngine::UIImage* capLeft_ = nullptr;
        CoreEngine::UIImage* capRight_ = nullptr;
        std::vector<Pip> pips_;
        std::vector<CoreEngine::UIImage*> leaves_;
        std::vector<CoreEngine::UIImage*> vines_;   ///< 板の縁に絡ませた蔦

        std::size_t visiblePipCount_ = 0;
        std::size_t fillFrontIndex_ = 0;  ///< 実っている粒のうち一番右。バナナの着地点
        float gainPopElapsed_ = 0.0f;     ///< バナナが入った反応の経過秒
        bool  gainPopActive_ = false;
        float lowPulse_ = 0.0f;   ///< 次の 1 マスも払えないときの警告演出（0〜1）
        float introReveal_ = 1.0f;  ///< 登場アニメーションの進み具合（1 = 定位置）
        std::size_t previewPipCount_ = 0; ///< 次の 1 マスで食べられる粒の数
        bool  built_ = false;
    };
}

#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"

#include <array>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    class UIImage;
    class UIText;
}

namespace GameComponents
{
    /// @brief ツタで吊るした木の看板で「つぎの目標距離」を提示する HUD。
    ///
    /// @details 上の板に「もくひょう」、下の板に「５００ｍ」のように距離を彫る。
    ///          天井から降りてきて行き過ぎてから戻り、しばらく揺れてから巻き上がる。
    ///          板・端木・蔦・茂みは**ポーズメニューと同じテクスチャ**を使うので、
    ///          新規アセットは無く、見た目の語彙もそのまま揃う。
    ///
    ///          `Show()` は何度でも呼べる。表示中に呼ぶと頭から降り直すので、
    ///          500m → 1000m → 1500m … と目標が切り替わるたびに使える。
    ///
    /// @note 突入演出の締めに出す「つなげ！！」も、同じフォントアトラスを使う都合で
    ///       このコンポーネントが持っている（`PlayStartCall()`）。看板とは独立に動く。
    ///
    /// @note 見た目と時間の値はすべて CVar `Game.ObjectiveSign.*` が持つ。
    ///       実行中にインスペクターの「ゲーム設定」から調整でき、CVars.json へ自動保存される。
    ///
    /// @note UI はトーンマップ前のシーンバッファへ描かれるので、色の指定はリニア値。
    ///       自動露出のぶんは `SetExposureScale()` で打ち消す（ポーズメニューと同じ）。
    class ObjectiveSignComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "ObjectiveSign"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "目標看板"; }
        bool DrawInspector() override;
#endif

        /// @brief 板・蔦・文字を生成する（アタッチ直後）
        void Awake() override;

        /// @brief 降り／滞在／巻き上げと、「つなげ！！」の演出を進める
        void Update() override;

        /// @brief 目標距離の看板を出す（表示中に呼ぶと頭から出し直す）
        /// @param meters 地面の距離目盛りと同じ単位のメートル数
        void Show(std::uint32_t meters);

        /// @brief 突入演出の締めに「つなげ！！」を叩き込む
        void PlayStartCall();

        /// @brief 看板が画面に出ているか
        bool IsShowing() const { return phase_ != Phase::Hidden; }

        /// @brief 自動露出の打ち消し倍率（`exp2(-AutoExposureEV)`）
        void SetExposureScale(float scale);

    private:
        /// @brief 1 段ぶんの板（中板＋左右の端木）
        struct Plank {
            CoreEngine::UIImage* mid = nullptr;
            CoreEngine::UIImage* capLeft = nullptr;
            CoreEngine::UIImage* capRight = nullptr;
        };

        enum class Phase { Hidden, Showing };

        void BuildParts();
        Plank SpawnPlankRow(const std::string& name, int sortOrder);
        void SetSignActive(bool active);

        /// @brief 降り（行き過ぎて戻る）→ 滞在 → 巻き上げ を 0..1 で返す
        float DropProgress() const;
        /// @brief 着地の反動と常時のそよぎを足した傾き [rad]
        float SwayAngle() const;

        void ApplySignLayout();
        void ApplyStartCall(float deltaTime);
        void PlacePlank(const Plank& plank, const CoreEngine::Vector2& center,
                        float width, float angle) const;

        Plank boardUpper_{};
        Plank boardLower_{};
        std::array<CoreEngine::UIImage*, 2> ropes_{};
        std::array<CoreEngine::UIImage*, 6> creepers_{};
        std::array<CoreEngine::UIImage*, 2> foliage_{};
        CoreEngine::UIText* label_ = nullptr;   ///< 「もくひょう」
        CoreEngine::UIText* number_ = nullptr;  ///< 「２００ｍ」
        CoreEngine::UIText* call_ = nullptr;    ///< 「つなげ！！」

        Phase phase_ = Phase::Hidden;
        float phaseTimer_ = 0.0f;
        float swayTimer_ = 0.0f;
        float callTimer_ = -1.0f;   ///< 負なら「つなげ！！」は出ていない
        float exposureScale_ = 1.0f;
        std::uint32_t meters_ = 0;
        bool built_ = false;
    };
}

#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class UIImage;
    class UIText;
    class MsdfFont;
}

namespace GameComponents
{
    /// @brief リザルトの「目標 500m までのゲージ」を組み立てて動かすコンポーネント。
    ///
    /// @details リザルトに足りていなかったのは飾りではなく **指標** だった。
    ///          「◯◯m 進んだ」だけでは良いのか悪いのか判断できないので、
    ///          比べる相手を 3 つ画面に出す。
    ///           - 目標（既定 500m）……画面下のゲージの終点に立つゲート
    ///           - 前回の自分  ……ゲージの上に立つ杭（`GameRecordStore`）
    ///           - 自己最高    ……右上の板。記録が無い回でも必ず出す
    ///          距離は 0 から数えあげ、トロッコが実際にレールを敷きながら走って止まる。
    ///
    ///          絵は既存の流用のみ。板・ツタ・茂み・葉カーソル・レールはポーズメニューと同じ
    ///          `Textures/Pause/*.png`（レールは板の中央パーツを引き伸ばして使う。
    ///          専用のレール絵は見た目が浮くので使わない）。トロッコ・駅だけはローディング
    ///          演出の `loading_*.png`。新規アセットは 1 枚も足していない。
    ///
    /// @note 背景（地面に埋まったサル）が主役なので、UI は画面の上端・下端・四隅にだけ置く。
    ///       y = 250〜760 には何も出さない。暗幕も掛けない。
    ///
    /// @note 位置はすべて基準解像度 1920x1080 の px。アンカーは TopCenter で統一し、
    ///       x は画面中央から、y は画面上端から測る。
    ///
    /// @note ゲージの刻みは 100m 固定。枕木（レールの縞）はその 100m を等分した位置に置くので、
    ///       目盛りの杭と必ず重なる。px を直に刻むと目盛りとずれるので、
    ///       間隔は必ず `TiePitch()` から取ること。
    ///
    /// @note 選択肢の木札は、シーンが動かす `ResultButtonAnimationComponent` 付きの
    ///       UIText に **板のほうが追従する** 作りにしてある。こうすると
    ///       ResultScene 側の選択・決定の処理へ手を入れずに見た目だけ差し替えられる。
    ///
    /// @note UI はトーンマップ前のバッファへ描かれるので、色の指定はリニア値。
    ///       自動露出のぶんは毎フレーム打ち消す（ポーズメニューと同じ）。
    class ResultGaugeUIComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> GoalMeters;
        static CoreEngine::CVar<float> CountSeconds;
        static CoreEngine::CVar<float> Brightness;
        static CoreEngine::CVar<float> RailBrightness;
        static CoreEngine::CVar<int> SortOrder;
        static CoreEngine::CVar<bool> ShowPreviousRecord;
        static CoreEngine::CVar<CoreEngine::Vector4> NumberColor;
        static CoreEngine::CVar<CoreEngine::Vector4> LabelColor;
        static CoreEngine::CVar<CoreEngine::Vector4> AccentColor;

        const char* GetTypeName() const override { return "ResultGaugeUI"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "リザルトゲージ"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }
        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 1.00f; outRgba[1] = 0.78f; outRgba[2] = 0.28f; outRgba[3] = 1.00f;
        }
        bool DrawInspector() override;
#endif

        /// @brief 板・ツタ・レール・文字をすべて生成する（アタッチ直後）
        void Awake() override;

        /// @brief カウントアップとレイアウトを 1 フレーム進める
        void Update() override;

        /// @brief シーンが選択・決定を駆動するための UIText（`Elements` へ渡す）
        /// @{
        CoreEngine::UIText* GetRetryText() const { return retry_.text; }
        CoreEngine::UIText* GetTitleText() const { return title_.text; }
        CoreEngine::UIText* GetTipText() const { return tipText_; }
        /// @}

    private:
        /// @brief 端木＋中板＋端木の 3 枚組
        struct Plank
        {
            CoreEngine::UIImage* mid = nullptr;
            CoreEngine::UIImage* capLeft = nullptr;
            CoreEngine::UIImage* capRight = nullptr;
        };

        /// @brief 選択肢の木札 1 枚（板は文字に追従する）
        struct Choice
        {
            Plank plank;
            std::array<CoreEngine::UIImage*, 3> creepers{};
            CoreEngine::UIText* text = nullptr;
            float baseFontSize = 44.0f;
            float baseY = 0.0f;
        };

        /// @brief 舞う葉 1 枚
        struct Leaf
        {
            CoreEngine::UIImage* image = nullptr;
            CoreEngine::Vector2 position{};
            CoreEngine::Vector2 velocity{};
            float rotation = 0.0f;
            float spin = 0.0f;
            float life = 0.0f;
            float lifeSpan = 1.0f;
            float size = 1.0f;
        };

        // ── 生成
        void BuildParts();
        CoreEngine::UIImage* SpawnImage(const char* texture, const std::string& name, int order);
        CoreEngine::UIText* SpawnText(CoreEngine::MsdfFont* font, const std::string& text,
                                      const std::string& name, float fontSize, int order);
        Plank SpawnPlank(const std::string& name, int order);
        void BuildHeadline(int order);
        void BuildSideBoards(int order);
        /// @brief 目盛り 1 つぶん（100m）を等分した、枕木 1 本ぶんの間隔 [px]
        float TiePitch() const;
        void BuildGauge(int order);
        void BuildChoices(int order);
        void BuildFooter(int order);

        // ── 配置
        void PlacePlank(const Plank& plank, const CoreEngine::Vector2& center,
                        float width, float height, float angle = 0.0f) const;
        void SetPlankColor(const Plank& plank, const CoreEngine::Vector4& color) const;
        void ApplyLayout(float deltaTime);
        void ApplyGauge();
        void ApplyChoice(Choice& choice, float centerX);
        void UpdateLeaves(float deltaTime);
        void BurstLeaves(const CoreEngine::Vector2& origin, int count, float power);

        // ── 値
        float GoalDistance() const;
        /// @brief 距離 [m] を画面 x（中央基準）へ写す
        float DistanceToX(float meters) const;
        CoreEngine::Vector4 Tinted(const CoreEngine::Vector4& color, float scale = 1.0f) const;

        // 見出し
        Plank headline_[2]{};
        std::array<CoreEngine::UIImage*, 12> headCreepers_{};
        std::array<CoreEngine::UIImage*, 2> headFoliage_{};
        std::array<CoreEngine::UIImage*, 5> headVines_{};
        CoreEngine::UIText* distanceText_ = nullptr;  ///< 「３８４ｍ」
        CoreEngine::UIText* remainText_ = nullptr;    ///< 「もくひょうまで あと １１６ｍ」

        // 左上
        Plank monkeyPlank_{};
        std::array<CoreEngine::UIImage*, 2> monkeyCreepers_{};
        std::array<CoreEngine::UIImage*, 2> monkeyVines_{};
        CoreEngine::UIText* monkeyText_ = nullptr;
        Plank rankPlank_{};
        CoreEngine::UIText* rankText_ = nullptr;

        // 右上（自己最高。操作説明はここから撤去した）
        Plank bestPlank_{};
        CoreEngine::UIText* bestText_ = nullptr;

        // ゲージ
        CoreEngine::UIImage* railBar_ = nullptr;  ///< 敷いた区間 1 本（板を伸縮させて使う）
        std::vector<CoreEngine::UIImage*> sleepers_;
        std::vector<CoreEngine::UIImage*> railVines_;
        std::vector<CoreEngine::UIImage*> tickPosts_;  ///< 100m ごとの目盛り（最後は目標地点）
        std::vector<CoreEngine::UIText*> tickTexts_;
        CoreEngine::UIImage* gatePost_ = nullptr;  ///< 目標看板を支える柱
        Plank gateBeam_{};
        std::array<CoreEngine::UIImage*, 2> gateFoliage_{};
        CoreEngine::UIText* gateText_ = nullptr;
        CoreEngine::UIImage* recordPost_ = nullptr;
        Plank recordPlank_{};
        CoreEngine::UIText* recordText_ = nullptr;
        Plank newRecordPlank_{};
        CoreEngine::UIText* newRecordText_ = nullptr;
        CoreEngine::UIImage* cart_ = nullptr;

        // 選択肢・足元
        Choice retry_{};
        Choice title_{};
        CoreEngine::UIImage* cursor_ = nullptr;
        CoreEngine::UIText* tipText_ = nullptr;
        CoreEngine::UIImage* tipLeaf_ = nullptr;
        CoreEngine::UIImage* tipBg_ = nullptr;  ///< Tips を読みやすくする半透明の帯
        std::array<CoreEngine::UIImage*, 2> corners_{};  ///< 上の 2 隅だけ

        static constexpr std::size_t kLeafPoolSize = 18;
        std::array<Leaf, kLeafPoolSize> leaves_{};

        // 進行
        std::uint32_t runMeters_ = 0;      ///< 今回の距離
        std::uint32_t previousMeters_ = 0; ///< 前回の距離（杭を立てる相手）
        std::uint32_t bestMeters_ = 0;     ///< 今回を含めた自己最高距離
        bool hasPrevious_ = false;
        bool isNewBest_ = false;
        float countTimer_ = 0.0f;
        float shownMeters_ = 0.0f;         ///< 数えあげ中の表示距離
        int lastTickIndex_ = -1;           ///< 直前に鳴らした 100m 目盛り
        bool arrived_ = false;             ///< 到着演出を出したか
        bool passedPrevious_ = false;      ///< 前回の杭を追い越した瞬間を鳴らしたか
        float swayTimer_ = 0.0f;
        float exposureScale_ = 1.0f;
        float cursorX_ = 0.0f;
        bool built_ = false;
        bool choiceColorApplied_ = false;  ///< 選択肢の文字色へ露出補正を入れ終えたか
        int displayedMeters_ = -1;         ///< 直前に SetText した整数距離
    };
}

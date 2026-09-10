#include "pch.h"
#include "RailDirectionGuideFeature.h"

#include "Components/Building/MapChipData.h"
#include "Components/Building/MapGeneratorComponent.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "Components/Rail/RailPathComponent.h"
#include "Components/UI/PauseMenuUIComponent.h"
#include "Components/Utility/BlockModelLayout.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Text3D/Text3DObject.h"
#include "Math/Vector/Vector4.h"
#include "Text/FontManager.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

using namespace CoreEngine;

namespace {

    // ──────────────────────────────────────────────────────────
    // レール先頭の進行方向ガイド
    // ──────────────────────────────────────────────────────────
    // 先頭マス（＝ビルダーがいるマス）の上下左右へ、床と平行な矢印を 1 つずつ置く。
    // 出るのは「そこへレールを伸ばせる向き」だけで、伸ばせない向き
    // （マップ外・空白・駅・バナナの木・既設レール）には何も出さない。
    // 「何も出ていない＝そっちへは行けない」と読ませたいので、
    // 行けないことを示す記号はあえて用意していない。
    //
    // ■ 戻る向きだけは別の記号にしてある
    //   直前のマスへの入力は敷設ではなく Undo で、レールが 1 本消えてスタミナが戻る。
    //   他の 3 方向とまったく同じ矢印にすると「4 方向へ伸ばせる」と読めてしまい、
    //   向きを変えるつもりで最後の 1 本を消す事故になる。位置は十字のまま残して
    //   （押せる向きであることは示す）、記号と色と大きさで意味を分ける。
    //   同じ矢印に戻したいときは Game.RailGuide.BackAsArrow を立てる。
    //   そもそも出したくないときは Game.RailGuide.ShowBack を切る。
    //
    // 値は CVars.json へ自動保存され、インスペクターの「ゲーム設定」から編集できる。

    CVar<bool> cvEnabled{
        "Game.RailGuide.Enabled", true,
        "レール先頭に進行方向の矢印を出す" };

    CVar<float> cvFontSize{
        "Game.RailGuide.FontSize", 0.6f,
        "矢印の大きさ（1 マスに対する割合）。1 マスぶんの大きさで 1.0",
        CVarRange{ 0.1f, 3.0f } };

    CVar<float> cvOffset{
        "Game.RailGuide.Offset", 1.0f,
        "先頭マスの中心から矢印までの距離（マス単位）。"
        "0.5 でマスの境目、1.0 で移動先マスの中心。"
        "小さくするとレールへ寄って先頭のカーソルと固まって見える",
        CVarRange{ 0.0f, 2.0f } };

    CVar<float> cvHeightOffset{
        "Game.RailGuide.HeightOffset", 0.3f,
        "レール上面から矢印を浮かせる高さ（マス単位）。"
        "地面すれすれだと地形の模様に紛れるので、少し浮かせて影の代わりに離す",
        CVarRange{ 0.0f, 2.0f } };

    CVar<Vector4> cvOutlineColor{
        "Game.RailGuide.OutlineColor", Vector4{ 0.0f, 0.0f, 0.0f, 1.0f },
        "矢印の縁取りの色。草・水・岩のどこに重なっても字が沈まないように付ける" };

    CVar<float> cvOutlineWidth{
        "Game.RailGuide.OutlineWidth", 0.085f,
        "矢印の縁取りの太さ（em 単位）。距離場が持つ情報量で上限が決まり"
        "（このフォントなら約 0.096em）、それを超える値は実際の上限へ丸める",
        CVarRange{ 0.0f, 0.1f } };

    CVar<float> cvPulseAmplitude{
        "Game.RailGuide.PulseAmplitude", 0.08f,
        "矢印が向いている方へ出入りする幅（マス単位）。0 で animation なしの固定表示",
        CVarRange{ 0.0f, 0.5f } };

    CVar<float> cvPulseSpeed{
        "Game.RailGuide.PulseSpeed", 3.5f,
        "矢印の出入りの速さ[rad/秒]。上げるほど細かく動く",
        CVarRange{ 0.0f, 20.0f } };

    CVar<Vector4> cvExtendColor{
        "Game.RailGuide.ExtendColor", Vector4{ 1.0f, 0.97f, 0.72f, 1.0f },
        "レールを伸ばせる向きの矢印の色" };

    CVar<Vector4> cvBackColor{
        "Game.RailGuide.BackColor", Vector4{ 1.0f, 0.52f, 0.42f, 1.0f },
        "戻る（Undo）向きの記号の色。伸ばせる向きと取り違えないよう別の色にしてある" };

    CVar<float> cvBackSizeScale{
        "Game.RailGuide.BackSizeScale", 0.8f,
        "戻る向きの記号の大きさ倍率。1 未満にすると進む向きより控えめになる",
        CVarRange{ 0.1f, 2.0f } };

    CVar<float> cvShortageAlpha{
        "Game.RailGuide.ShortageAlpha", 0.3f,
        "スタミナが足りずに敷けない向きの不透明度。1 にすると足りるときと同じ見た目になる",
        CVarRange{ 0.0f, 1.0f } };

    CVar<bool> cvShowBack{
        "Game.RailGuide.ShowBack", true,
        "戻る（Undo）向きにも記号を出す。切ると進める向きの矢印だけになる" };

    CVar<bool> cvBackAsArrow{
        "Game.RailGuide.BackAsArrow", false,
        "戻る向きも他と同じ矢印で出す。意味が違う（レールが消える）ので既定は切ってある" };

    /// フォントは距離目盛りと同じピクセルフォント。U+2190..2193（←↑→↓）を持っている
    constexpr const wchar_t* kFontPath = L"Engine/Assets/font/x8y12pxDenkiChip.ttf";

    /// 戻る向きの記号。「進む矢印ではない」と一目で分かる形にする
    constexpr const char* kBackGlyph = "×";

    /// @brief ガイドを出す 4 方向
    struct GuideDirection {
        int32_t dx;
        int32_t dz;
        const char* arrow;
    };

    // 床と平行に寝かせた文字は、字の右が +X・字の上が +Z を向く
    // （transform.rotate.x = 90 度。距離目盛りと同じ置き方）。
    // ゲームカメラは -Z 側から見下ろすので、画面の右が +X・画面の奥が +Z になり、
    // 矢印の向きと移動入力の向きがそのまま一致する。
    constexpr std::array<GuideDirection, 4> kDirections{ {
        { +1,  0, "→" },
        { -1,  0, "←" },
        {  0, +1, "↑" },
        {  0, -1, "↓" },
    } };

    /// @brief 1 方向ぶんの表示状態
    enum class GuideState {
        Hidden,   ///< 何も出さない（そっちへは伸ばせない）
        Extend,   ///< レールを伸ばせる
        Shortage, ///< 伸ばせる向きだが、いまはスタミナが足りない
        Back,     ///< 直前のマス。押すと Undo（レールが消えてスタミナが戻る）
    };

    using Cell = std::pair<int32_t, int32_t>;

    /// @brief レールの先頭と、その直前のマスをまとめたもの
    struct RailHead {
        Cell head{ 0, 0 };
        Cell previous{ 0, 0 };
        /// 直前のマスがあるか（＝Undo できるか）。未確定レールが 1 本も無いと false
        bool hasPrevious = false;
    };

    /// @brief レールの先頭（ビルダーがいるマス）と直前のマスを求める
    /// @details RailBuilderComponent が移動先を判定するときと同じ決まり方にしてある。
    ///          未確定レールがあればその末尾が先頭で、無ければ確定済みレールの末尾。
    bool TryGetRailHead(GameComponents::RailPathComponent& railPath, RailHead& out)
    {
        const auto& railMap = railPath.GetRailMap();
        const auto& undoStack = railPath.GetRailUndoStack();
        if (railMap.empty()) {
            return false;
        }

        out.head = undoStack.empty() ? railMap.back() : undoStack.back();
        // Undo できるのは未確定レールがあるときだけ。列車が追いついて全部確定すると、
        // 直前のマスへ入力しても撤去は起きない（RailBuilderComponent と同じ条件）
        out.hasPrevious = !undoStack.empty();
        out.previous = (undoStack.size() >= 2)
            ? undoStack[undoStack.size() - 2]
            : railMap.back();
        return true;
    }

    /// @brief そのマスに既にレールがあるか
    bool HasRailAt(GameComponents::RailPathComponent& railPath, const Cell& cell)
    {
        const auto& railMap = railPath.GetRailMap();
        const auto& undoStack = railPath.GetRailUndoStack();
        return std::find(railMap.begin(), railMap.end(), cell) != railMap.end() ||
            std::find(undoStack.begin(), undoStack.end(), cell) != undoStack.end();
    }

    /// @brief 1 マス敷くのに要るスタミナを求める
    /// @note RailBuilderComponent が実際に消費する式と同じ。ずれると
    ///       「矢印は出ているのに敷けない」が起きるので、変えるときは両方を直すこと
    float CalcPlacementCost(
        const GameComponents::MapGeneratorComponent& mapGenerator,
        const GameComponents::HungerComponent& hunger,
        const Cell& cell)
    {
        const auto x = static_cast<std::size_t>(cell.first);
        const auto z = static_cast<std::size_t>(cell.second);

        const float railCost = mapGenerator.IsStationRailCell(x, z)
            ? 0.0f
            : std::max(0.0f, GameComponents::GameSettings::RailStaminaCost.Get());
        float baseCost = railCost;

        const GameComponents::MapChipType mapChip = mapGenerator.GetMapChip(x, z);
        if (mapChip == GameComponents::MapChipType::Water) {
            baseCost += std::max(0.0f, GameComponents::GameSettings::BridgeStaminaCost.Get());
        } else if (mapChip == GameComponents::MapChipType::Resource) {
            baseCost += std::max(0.0f, GameComponents::GameSettings::RockStaminaCost.Get());
        }

        return hunger.CalculateActionCost(baseCost);
    }

    // ──────────────────────────────────────────────────────────
    // Feature
    // ──────────────────────────────────────────────────────────

    /// @brief レール先頭の上下左右へ、進める向きだけ矢印を出す Feature
    /// @details 先頭の位置は RailPathComponent から読むだけで、ゲーム側の状態は変えない
    ///          （マップの生成も促さない。描画範囲ぶんは MapViewComponent が先に伸ばしている）。
    /// 突入演出あけの登場進捗。Feature の外から渡されるので Feature の外に置く
    float g_introReveal = 1.0f;

    class RailDirectionGuideFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "RailDirectionGuide"; }

        /// @details シーンの OnInitialize() が終わった後に呼ばれるフックなので、
        ///          この時点ならレール・マップ・スタミナのコンポーネントが揃っている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            g_introReveal = 1.0f;

            railPath_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::RailPathComponent>();
            mapGenerator_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::MapGeneratorComponent>();
            hunger_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::HungerComponent>();
            // ゲームオーバーで止まったときに矢印も引っ込めるために見ている。無くても動く
            railBuilder_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::RailBuilderComponent>();

            if (!railPath_ || !mapGenerator_ || !hunger_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "RailDirectionGuideFeature: 必要なコンポーネントが見つからないため矢印を出しません");
                return;
            }

            MsdfFont* font = nullptr;
            if (auto* fontManager = ctx.engine ? ctx.engine->GetService<FontManager>() : nullptr) {
                MsdfFontDesc fontDesc;
                fontDesc.filePath = kFontPath;
                fontDesc.systemFamilyNames = { L"Yu Gothic UI", L"Meiryo", L"MS Gothic" };
                fontDesc.charsetUtf8 = "←↑→↓×";
                // 使うのは記号だけなので ASCII は焼かない（アトラスを小さく保つ）
                fontDesc.includeAscii = false;
                font = fontManager->Acquire(fontDesc);
            }
            if (!font) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "RailDirectionGuideFeature: 矢印用のフォントを取得できませんでした");
                return;
            }

            for (std::size_t i = 0; i < markers_.size(); ++i) {
                auto* marker = ctx.gameObjectManager->AddObject(std::make_unique<Text3DObject>());
                if (!marker) {
                    break;
                }
                marker->Initialize(
                    font, kDirections[i].arrow, "RailDirectionGuide_" + std::to_string(i));
                // 毎フレーム置き直すので、シーンには保存しない
                marker->SetSerializeEnabled(false);
                // 床へ寝かせて置くので、カメラへは向けない
                marker->SetBillboard(Text3DBillboard::None);
                // 深度テストは切って常に手前へ出す。岩マスはレールを敷ける＝矢印を出す
                // 対象なのに、岩は 1 マスぶんの背があるので、深度テストありだと
                // 「行けるのに矢印が岩へ埋まって見えない」が起きる
                marker->SetDepthMode(Text3DDepthMode::Overlay);
                // 色・縁取りは CVar を毎フレーム反映するので、ここでは触らない
                marker->SetActive(false);
                markers_[i] = marker;
            }
        }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override
        {
            // レール先頭が動くのは RailBuilderComponent の Update なので、
            // GameObject の更新が終わったこのフェーズで読む
            if (phase != SceneUpdatePhase::PostObjectUpdate) {
                return;
            }

            // ポーズメニューは PauseMenuFeature が PostSceneInitialize で作る。
            // Feature の登録順に依存しないよう、全 Feature の初期化が終わった
            // 最初の Update で 1 度だけ引く
            if (!pauseMenuResolved_) {
                if (ctx.gameObjectManager) {
                    pauseMenu_ = ctx.gameObjectManager
                        ->FindFirstComponent<GameComponents::PauseMenuUIComponent>();
                }
                pauseMenuResolved_ = true;
            }

            // 位相を貯める（角度を貯めるので、速さを変えても飛ばずに繋がる）。
            // そのままだと桁が溢れて sin の精度が落ちるので 1 周ごとに畳む
            pulsePhase_ += Time::DeltaTime() * cvPulseSpeed.Get();
            constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;
            if (pulsePhase_ >= kTwoPi) {
                pulsePhase_ = std::fmod(pulsePhase_, kTwoPi);
            }

            RailHead railHead{};
            const bool show = cvEnabled.Get() && railPath_ && mapGenerator_ && hunger_ &&
                // 突入演出のあいだは出さない（GameEntranceFeature が 0 を渡してくる）
                g_introReveal > 0.0f &&
                (!railBuilder_ || railBuilder_->IsEnabled()) &&
                // ポーズ中は操作できないので、進める向きの案内も引っ込める。
                // 深度テストを切ってある＝看板の手前にも出てしまうので、なおさら消す
                !(pauseMenu_ && pauseMenu_->IsOpen()) &&
                TryGetRailHead(*railPath_, railHead);
            if (!show) {
                HideAll();
                return;
            }

            // マスの大きさは GameScene が RailBuilder / MapView へ渡すのと同じ CVar から取る
            const float gridSize = std::max(0.01f, GameComponents::GameSettings::GridSize.Get());
            // レールの上面より上へ置く。先頭マスにも直前のマスにもレールが乗っているので、
            // 地面の高さに合わせるとレールへ埋まってしまう
            const float height = GameComponents::BlockModelLayout::GetRailTopHeight(gridSize) +
                cvHeightOffset.Get() * gridSize;
            // 向いている方へ出たり戻ったりさせる。止まった記号だと画面の中で沈むので、
            // 「押せる」ことを動きで伝える。4 つとも同じ位相にして 1 組に見せる
            const float offset =
                cvOffset.Get() + std::sin(pulsePhase_) * cvPulseAmplitude.Get();

            for (std::size_t i = 0; i < markers_.size(); ++i) {
                UpdateMarker(markers_[i], kDirections[i], railHead, gridSize, height, offset);
            }
        }

        /// @brief 停止中も回す（止めると「ゲーム設定」で値を変えても矢印が動かない）
        bool RunsWhileStopped() const override { return true; }

        /// @note 矢印はシーンの GameObject なので、破棄はシーン側で済んでいる。
        ///       残った参照だけ畳む
        void PostSceneFinalize(SceneContext&) override
        {
            markers_.fill(nullptr);
            railPath_ = nullptr;
            mapGenerator_ = nullptr;
            hunger_ = nullptr;
            railBuilder_ = nullptr;
            pauseMenu_ = nullptr;
            pauseMenuResolved_ = false;
        }

    private:
        /// @brief その向きへレールを伸ばせるかを判定する
        /// @note RailBuilderComponent の移動判定と同じ順番で見ている。
        ///       特に「直前のマスか」は既設レールの判定より先（向こうも Undo が先）
        GuideState Classify(const GuideDirection& direction, const RailHead& railHead) const
        {
            const Cell next{
                railHead.head.first + direction.dx,
                railHead.head.second + direction.dz };

            if (next.first < 0 || next.second < 0 ||
                next.second >= static_cast<int32_t>(railPath_->GetMapSizeZ())) {
                return GuideState::Hidden;
            }
            if (railHead.hasPrevious && next == railHead.previous) {
                return GuideState::Back;
            }
            if (HasRailAt(*railPath_, next)) {
                return GuideState::Hidden;
            }
            if (!mapGenerator_->CanConnectRail(
                    railHead.head.first, railHead.head.second, next.first, next.second)) {
                return GuideState::Hidden;
            }

            const float cost = CalcPlacementCost(*mapGenerator_, *hunger_, next);
            return (hunger_->GetCurrentHunger() >= cost)
                ? GuideState::Extend
                : GuideState::Shortage;
        }

        /// @brief 1 方向ぶんの矢印を置き直す
        void UpdateMarker(
            Text3DObject* marker, const GuideDirection& direction, const RailHead& railHead,
            float gridSize, float height, float offset) const
        {
            if (!marker) {
                return;
            }

            const GuideState state = Classify(direction, railHead);
            const bool isBack = (state == GuideState::Back);
            if (state == GuideState::Hidden || (isBack && !cvShowBack.Get())) {
                marker->SetActive(false);
                return;
            }

            const bool useBackGlyph = isBack && !cvBackAsArrow.Get();
            marker->SetActive(true);
            marker->SetText(useBackGlyph ? kBackGlyph : direction.arrow);
            // 登場アニメーション中は同じ場所で伸び上がらせる。床へ寝かせてあるので、
            // 位置を動かすより大きさを変えたほうが「生えてきた」に見える
            marker->SetFontSize(
                cvFontSize.Get() * gridSize * g_introReveal
                * (isBack ? cvBackSizeScale.Get() : 1.0f));

            Vector4 color = isBack ? cvBackColor.Get() : cvExtendColor.Get();
            if (state == GuideState::Shortage) {
                // 敷ける向きではあるので消さず、薄くして「いまは足りない」と見せる
                color.w *= cvShortageAlpha.Get();
            }
            marker->SetColor(color);

            // 縁取りは距離場が持てる幅で頭打ちにする。
            // 超えた値を渡すとクワッドの端で切れて、字のまわりに矩形が出る
            Vector4 outlineColor = cvOutlineColor.Get();
            if (state == GuideState::Shortage) {
                outlineColor.w *= cvShortageAlpha.Get();
            }
            marker->SetOutline(
                outlineColor, std::min(cvOutlineWidth.Get(), marker->GetMaxOutlineWidth()));

            auto* transformComponent = marker->GetComponent<TransformComponent>();
            if (!transformComponent) {
                return;
            }
            auto& transform = transformComponent->Get();
            transform.translate = {
                (static_cast<float>(railHead.head.first) +
                    static_cast<float>(direction.dx) * offset) * gridSize,
                height,
                (static_cast<float>(railHead.head.second) +
                    static_cast<float>(direction.dz) * offset) * gridSize
            };
            // 床と平行に寝かせる。距離目盛りと同じ向きで、上から読める
            transform.rotate = { std::numbers::pi_v<float> * 0.5f, 0.0f, 0.0f };
            // このフェーズは TransformComponent の更新より後なので、自分で行列へ流す
            transform.TransferMatrix();
        }

        void HideAll() const
        {
            for (auto* marker : markers_) {
                if (marker) {
                    marker->SetActive(false);
                }
            }
        }

        std::array<Text3DObject*, kDirections.size()> markers_{};

        /// 出入りの位相[rad]。1 周ごとに畳んでいる
        float pulsePhase_ = 0.0f;

        GameComponents::RailPathComponent* railPath_ = nullptr;
        GameComponents::MapGeneratorComponent* mapGenerator_ = nullptr;
        GameComponents::HungerComponent* hunger_ = nullptr;
        GameComponents::RailBuilderComponent* railBuilder_ = nullptr;

        /// ポーズ中に矢印を引っ込めるために見ている。無くても動く
        GameComponents::PauseMenuUIComponent* pauseMenu_ = nullptr;
        bool pauseMenuResolved_ = false;

    public:
        /// @brief 登場進捗を既定へ戻す（次のシーンへ持ち出さないため）
        void Finalize(SceneContext&) override { g_introReveal = 1.0f; }
    };
}

void GameComponents::SetRailDirectionGuideReveal(float reveal)
{
    g_introReveal = std::clamp(reveal, 0.0f, 2.0f);
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateRailDirectionGuideFeature()
{
    return std::make_unique<RailDirectionGuideFeature>();
}

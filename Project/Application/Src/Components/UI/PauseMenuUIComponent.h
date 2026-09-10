#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace CoreEngine
{
    class UIImage;
    class UIText;
}

namespace GameComponents
{
    /// @brief ツタで吊るした木の看板のポーズメニュー
    /// @details 絵は `Assets/Textures/Pause/*.png`（版下は
    ///          `Project/Build/Scripts/gen_pause_tex.py`）。板・端木・垂れ蔦・
    ///          這い蔦・葉の茂みを組み合わせて、ジャングルに置かれた看板に見せる。
    ///          開くと画面が沈み、天井から蔦が伸びて看板が落ちてくる。木札 3 枚が
    ///          そのあとを追ってぶら下がり、選択中の札はツタを支点に傾いて少し
    ///          大きくなる。決定すると札がへこんで葉が舞う。
    ///
    /// @note ポーズ中は `PlaybackStateManager` がゲームの更新ごと止めるため、
    ///       この Update() は呼ばれない。動かすのは `Tick()` で、
    ///       停止中も回る `PauseMenuFeature` が毎フレーム叩く。
    /// @note 位置はすべて基準解像度 1920x1080 の px。テクスチャは等倍で並べ、
    ///       伸ばすのは横方向に一様な中板だけに限っている
    ///       （這い蔦のように模様のあるものは、伸ばさず並べてつなぐ）。
    class PauseMenuUIComponent final : public CoreEngine::IComponent
    {
    public:
        /// @brief メニューの項目。並び順がそのまま画面の上から下になる
        enum class Choice : std::size_t
        {
            Continue = 0, ///< つづける
            Retry,        ///< さいしょから
            Title,        ///< タイトルへ
            Count
        };

        const char* GetTypeName() const override { return "PauseMenuUI"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "ポーズメニュー"; }
        bool DrawInspector() override;
#endif

        /// @brief 板・蔦・木札・文字・葉を生成する（アタッチ直後）
        void Awake() override;

        /// @brief 何もしない
        /// @details ポーズ中は呼ばれない。動かすのは Feature から呼ばれる Tick()。
        void Update() override {}

        /// @brief 開閉アニメーションと葉を 1 フレーム進める（ポーズ中も呼ばれる）
        /// @param unscaledDeltaTime ポーズの影響を受けない実測の経過秒
        void Tick(float unscaledDeltaTime);

        /// @brief 落ちてくる演出を始める
        void Open();
        /// @brief 跳ね上がって消える演出を始める
        void Close();

        /// @brief 開いている（＝入力を受け付ける）か
        bool IsOpen() const { return phase_ == Phase::Opening || phase_ == Phase::Idle; }
        /// @brief 画面に何か出ているか（閉じる演出の途中を含む）
        bool IsVisible() const { return phase_ != Phase::Hidden; }

        /// @brief 選択を上下に動かす
        /// @param delta -1 で上、+1 で下。端では折り返す
        void MoveSelection(int delta);
        Choice GetSelection() const { return static_cast<Choice>(selection_); }

        /// @brief 決定したときの「札がへこんで葉が舞う」演出を鳴らす
        void PlayConfirm();

        /// @brief 画面右上の操作ヒントを、繋がっている入力機器に合わせる
        /// @param connected true でパッド（START）、false でキーボード（ESC）
        void SetHintForGamepad(bool connected);

        /// @brief 自動露出のぶんを打ち消す倍率を渡す
        /// @param scale exp2(-自動EV)。1.0 で補正なし
        /// @details UI もトーンマップ前のバッファへ描かれるので、掛からないと
        ///          明るい場所と暗い場所でメニューの色が変わってしまう。
        void SetExposureScale(float scale);

        /// @brief 突入演出あけの登場アニメーションの進み具合
        /// @param reveal 0 = 画面外へ引っ込んだ状態 ／ 1 = 定位置。
        ///               1 を少し超える値を渡すと行き過ぎて戻る（EaseOutBack を通した値をそのまま渡す想定）
        /// @details 誰も呼ばなければ 1 のままなので、従来どおり最初から出たままになる。
        ///          駆動するのは GameEntranceFeature。
        void SetIntroReveal(float reveal);

    private:
        /// @brief 開閉の進み具合
        enum class Phase
        {
            Hidden,  ///< 出ていない（画面右上の操作ヒントだけ出す）
            Opening, ///< 落ちてくる途中
            Idle,    ///< 出しきって操作できる
            Closing, ///< 跳ね上がって消える途中
        };

        /// @brief 板 1 枚ぶん（端木・中板・端木の 3 枚組）
        struct Plank
        {
            CoreEngine::UIImage* capLeft = nullptr;
            CoreEngine::UIImage* mid = nullptr;
            CoreEngine::UIImage* capRight = nullptr;
        };

        /// @brief 木札 1 枚ぶんの表示状態
        struct Item
        {
            Plank plank;
            CoreEngine::UIImage* ropeLeft = nullptr;   ///< 上の板からぶら下げる蔦
            CoreEngine::UIImage* ropeRight = nullptr;
            std::array<CoreEngine::UIImage*, 2> creepers{}; ///< 板の上を這う蔦
            CoreEngine::UIText* text = nullptr;
            float select = 0.0f; ///< 選択の度合い（0〜1。傾きと大きさに効く）
        };

        /// @brief 舞う葉 1 枚
        struct Leaf
        {
            CoreEngine::UIImage* image = nullptr;
            CoreEngine::Vector2 position{};
            CoreEngine::Vector2 velocity{};
            float rotation = 0.0f;
            float spin = 0.0f;
            float life = 0.0f;      ///< 残り秒数（0 で消える）
            float lifeSpan = 1.0f;
            float size = 1.0f;
        };

        void BuildParts();
        /// @brief 3 枚組の板を 1 つ生む
        Plank SpawnPlank(const std::string& name, int sortOrder);
        /// @brief 位置・大きさ・傾きを毎フレーム置き直す（CVar の変更も即反映するため）
        void ApplyLayout(float unscaledDeltaTime);
        /// @brief 板 1 枚を、中心・大きさ・傾きから配置する
        void PlacePlank(const Plank& plank, const CoreEngine::Vector2& center,
                        float width, float scale, float scaleY, float angle);
        /// @brief 縦の蔦を、上端と長さから配置する（足りなければ縦に詰める）
        void PlaceRope(CoreEngine::UIImage* rope, float centerX, float topY, float length);
        /// @brief 落下の進み具合（0 = 画面外の上、1 = 定位置）
        float DropProgress(float delaySeconds) const;
        /// @brief 舞う葉を進める
        void UpdateLeaves(float unscaledDeltaTime);
        /// @brief 葉を撒く
        /// @param origin 撒く位置（基準解像度の px。x は画面中央からのずれ）
        /// @param count 枚数
        /// @param power 飛び散る勢い
        void BurstLeaves(const CoreEngine::Vector2& origin, int count, float power);
        /// @brief メニュー本体の表示・非表示
        void SetMenuActive(bool active);
        /// @brief 画面右上の操作ヒントの表示・非表示
        void SetHudActive(bool active);

        static constexpr std::size_t kItemCount = static_cast<std::size_t>(Choice::Count);
        /// 看板を吊るす蔦の本数
        static constexpr std::size_t kHangingVineCount = 7;
        /// 蔦 1 本あたりのコマ数（縦に継いで天井まで届かせる）
        static constexpr std::size_t kVineSegmentCount = 4;
        /// 看板を這う蔦のコマ数（前半が上の縁、後半が下の縁）
        static constexpr std::size_t kBoardCreeperCount = 16;
        /// 葉の茂み（看板の肩と画面の四隅）
        static constexpr std::size_t kFoliageCount = 8;
        /// 使い回す葉の枚数
        static constexpr std::size_t kLeafPoolSize = 28;

        CoreEngine::UIImage* dim_ = nullptr;      ///< 背景を沈める暗幕
        std::array<CoreEngine::UIImage*, kHangingVineCount * kVineSegmentCount> vines_{};
        std::array<CoreEngine::UIImage*, kBoardCreeperCount> creepers_{};
        std::array<CoreEngine::UIImage*, kFoliageCount> foliage_{};
        Plank board_;                             ///< 看板（板 2 枚を縦に重ねる）
        Plank boardLower_;
        CoreEngine::UIText* title_ = nullptr;
        CoreEngine::UIImage* cursor_ = nullptr;   ///< 選択中の札に刺さる葉
        std::array<Item, kItemCount> items_{};
        std::array<Leaf, kLeafPoolSize> leaves_{};

        Plank hudPlank_;                          ///< 画面右上のポーズ操作ヒント
        CoreEngine::UIText* hudText_ = nullptr;
        CoreEngine::UIImage* hudLeaf_ = nullptr;

        Phase phase_ = Phase::Hidden;
        float phaseTimer_ = 0.0f;    ///< フェーズに入ってからの経過秒
        std::size_t selection_ = 0;
        float cursorY_ = 0.0f;       ///< 葉カーソルの追従位置（滑らせるため保持する）
        float confirmTimer_ = 0.0f;  ///< 決定演出の残り秒数
        float swayTimer_ = 0.0f;     ///< 蔦と葉を揺らす位相
        float exposureScale_ = 1.0f; ///< 自動露出を打ち消す倍率
        float introReveal_ = 1.0f;   ///< 操作ヒントの登場アニメーションの進み具合（1 = 定位置）
        bool gamepadHint_ = false;
        bool hudActive_ = false;
        bool built_ = false;
    };
}

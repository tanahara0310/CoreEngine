#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <vector>

namespace CoreEngine
{
    class Camera;
    class UIImage;
    class UIText;
}

namespace GameComponents
{
    class TrainMovementComponent;
    class GameManagerComponent;

    /// @brief トロッコが画面から外れている間だけ、画面の端にトロッコのアイコンと
    ///        「あと○m」を出して、戻ってくるまでの残りを見せる HUD。
    /// @details ゲームのカメラはトロッコとカーソルの中点を写す（Presets/CameraRigs/GamePlay.json）。
    ///          カーソルを先へ伸ばすほど中点も先へ動くので、置いていかれたトロッコは
    ///          画面の左へ押し出される。押し出されたことは画面から消えるので分かるが、
    ///          「あとどれだけで戻るのか」は何も出ていないと読めない。それを出すのがこれ。
    /// @note 出す数字は「トロッコが枠の中へ収まるまでに詰めるべきワールド距離」で、
    ///       0 になった瞬間にトロッコが丸ごと画面へ入る。カメラの構図に依らずこの対応が
    ///       保てるので、リグを差し替えても数字と見た目がずれない。
    ///       トロッコ自身が進む距離ではないことに注意（カメラも半分ぶん前へ出るので、
    ///       カーソルを止めたときトロッコはこの倍の距離を走って追いつく）。
    /// @note 見切れの判定はトロッコの中心ではなく外形（`TrolleyRadius`）で行う。
    ///       カメラがトロッコとカーソルの中点を写す構図では、カーソルが枠の内側で
    ///       止められている限りトロッコの中心もほぼ枠の中に残るため、中心で測ると
    ///       画面の端で大きく見切れていても案内が出ない。
    /// @note 1 ワールド単位 = 1m。地面の 5m 目盛り（MapViewComponent）と
    ///       リザルトの進行距離（GameResultData）と同じ換算にしてある。
    /// @note アイコンはローディング画面のトロッコ（loading_cart.png）、板と端木は
    ///       スタミナゲージのものを流用しているので、新規アセットは無い。
    ///       見た目の調整は CVar `Game.TrainOffscreen.*`（インスペクターの「画面外トロッコ案内」）で行う。
    class OffscreenTrainIndicatorUIComponent final : public CoreEngine::IComponent
    {
    public:
        OffscreenTrainIndicatorUIComponent(
            TrainMovementComponent* train = nullptr,
            CoreEngine::Camera* viewCamera = nullptr,
            GameManagerComponent* gameManager = nullptr)
            : train_(train), viewCamera_(viewCamera), gameManager_(gameManager) {}

        const char* GetTypeName() const override { return "OffscreenTrainIndicatorUI"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "画面外トロッコ案内"; }
        bool DrawInspector() override;
#endif

        /// @brief アイコン・矢印・板・文字を生成する（アタッチ直後）
        void Awake() override;
        /// @brief 画面外かを測り、出し入れと配置を進める
        void Update() override;

    private:
        /// @brief 画面外の測定結果
        struct Measurement {
            CoreEngine::Vector2 canvasPosition{}; ///< トロッコの投影先［基準解像度 px］。画面外なら枠の外を指す
            CoreEngine::Vector2 outward{};        ///< 画面外へ向かう向き（各軸 -1 / 0 / +1）
            float metersToEdge = 0.0f;            ///< 画面の端まで詰めるべき距離［m］
            bool offscreen = false;               ///< トロッコの中心が枠の外か
        };

        /// @brief アイコン・矢印・板・文字を組み立てる。オーナー自身がアイコンになる
        void BuildParts();
        /// @brief 今のカメラでトロッコを測る。カメラが無いなど測れないときは false
        bool Measure(Measurement& out) const;
        /// @brief 出す／引っ込めるを決める。境目でのちらつきを閾値で止める
        void UpdateVisibility(const Measurement& measurement, float deltaTime);
        /// @brief 残り距離の表示を更新する（変わった時だけ文字を組み直す）
        void UpdateDistanceLabel(float meters);
        /// @brief アイコン・矢印・板・文字を並べ直す
        void LayoutParts(const Measurement& measurement, float time);
        /// @brief 表示中のパーツをまとめて表示／非表示にする
        void SetPartsActive(bool active);

        TrainMovementComponent* train_ = nullptr;
        CoreEngine::Camera* viewCamera_ = nullptr;
        GameManagerComponent* gameManager_ = nullptr;

        CoreEngine::UIImage* icon_ = nullptr;      ///< トロッコのアイコン（オーナー自身）
        CoreEngine::UIText* arrow_ = nullptr;      ///< 画面外を指す三角
        CoreEngine::UIImage* board_ = nullptr;     ///< 「あと○m」を載せる中板
        CoreEngine::UIImage* boardCapLeft_ = nullptr;
        CoreEngine::UIImage* boardCapRight_ = nullptr;
        CoreEngine::UIText* prefixLabel_ = nullptr; ///< 「あと」
        CoreEngine::UIText* distanceLabel_ = nullptr; ///< 「12m」

        float elapsed_ = 0.0f;      ///< 揺れ用の経過秒
        float visibility_ = 0.0f;   ///< 0 = 消えている／1 = 出きっている
        bool wantsVisible_ = false; ///< 閾値で決めた「出したい」状態
        int shownMeters_ = -1;      ///< 今出している数字。-1 は未設定
        float shownDistanceWidth_ = 0.0f; ///< 「12m」の実測幅［px］。板の幅に使う
        float shownPrefixWidth_ = 0.0f;   ///< 「あと」の実測幅［px］
        bool built_ = false;
    };
}

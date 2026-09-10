#pragma once

#include "Audio/SoundInstance.h"
#include "GameObject/Component/Core/IComponent.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>

namespace CoreEngine
{
    class TransformComponent;
    class Camera;
}

namespace GameComponents {
    class RailPathComponent;
    class MapGeneratorComponent;
    class ModelRenderPoolComponent;
}

namespace GameComponents
{
    // 入力に応じてグリッド単位で移動するコンポーネント,レールパスが必要
    class RailViewComponent final
        : public CoreEngine::IComponent {
    public:
        explicit RailViewComponent(
            float gridSize = 5.0f,
            GameComponents::RailPathComponent* railPath = nullptr,
            GameComponents::ModelRenderPoolComponent* railPool = nullptr,
            GameComponents::ModelRenderPoolComponent* railLeftPool = nullptr,
            GameComponents::ModelRenderPoolComponent* railRightPool = nullptr,
            GameComponents::ModelRenderPoolComponent* bridgePool = nullptr,
            GameComponents::MapGeneratorComponent* mapGenerator = nullptr,
            CoreEngine::Camera* viewCamera = nullptr,
            std::function<void(float, float, bool)> onRailBuildSE = nullptr,
            uint32_t viewDistanceX = 30)
            : gridSize_(gridSize), railPath_(railPath),
            railPool_(railPool),
            railLeftPool_(railLeftPool),
            railRightPool_(railRightPool),
            bridgePool_(bridgePool),
            mapGenerator_(mapGenerator),
            viewCamera_(viewCamera),
            viewDistanceX_(viewDistanceX),
            onRailBuildSE_(onRailBuildSE) {
        }

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "RailView";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "レール描画"; }
        bool DrawInspector() override;
#endif

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;

        // グリッドサイズを設定する
        void SetGridSize(float size);
        // 中心位置を設定する
        void SetCenterPosition(const CoreEngine::Vector3& position);

    private:
        // 矢印で新しく置かれたレールを、その場で跳ねさせる
        void UpdateRailJumpAnimations(float deltaTime);
        float GetRailJumpOffset(std::size_t pathIndex) const;
        // バナナの木に隣接する新設レールだけ、ジャンプ中にY軸回転させる
        float GetBananaBuildRotation(std::size_t pathIndex,
            int32_t gridX, int32_t gridZ) const;
        // 列車が通過して確定したレールに、確定順に少しずつ遅らせてSEを鳴らす
        void UpdateConfirmationSounds(float deltaTime);

        // レール経路を直線・左コーナー・右コーナーへ分類してモデルを描画する
        void DrawRailModels();

        CoreEngine::TransformComponent* transform_ = nullptr;
        GameComponents::RailPathComponent* railPath_ = nullptr;

        GameComponents::ModelRenderPoolComponent* railPool_ = nullptr;
        GameComponents::ModelRenderPoolComponent* railLeftPool_ = nullptr;
        GameComponents::ModelRenderPoolComponent* railRightPool_ = nullptr;
        GameComponents::ModelRenderPoolComponent* bridgePool_ = nullptr;
        GameComponents::MapGeneratorComponent* mapGenerator_ = nullptr;
        // 描画範囲はゲーム視点カメラの位置から決める（構図は CameraRig が握る）
        CoreEngine::Camera* viewCamera_ = nullptr;
        float gridSize_ = 5.0f;
        uint32_t viewDistanceX_ = 30;
        float railJumpHeight_ = 0.8f;
        float railJumpDuration_ = 0.35f;
        float bananaBuildRotationTurns_ = 1.0f;
        float confirmationStaggerInterval_ = 0.06f;
        float confirmationSeVolume_ = 0.45f;
        float confirmationSeBasePitch_ = 0.9f;
        float confirmationSePitchStep_ = 0.05f;
        float confirmationSeMaxPitch_ = 1.35f;

        // 確定済み＋未確定を連結した経路の添字と対応する。置かれた瞬間から進む
        std::vector<float> railJumpTimes_;
        // railMap のインデックスと対応する。負値は再生開始までの待ち時間
        std::vector<float> confirmationSoundTimes_;
        std::vector<float> confirmationSoundPitches_;

        // 引数は音量、ピッチ、駅レールかどうかの順
        std::function<void(float, float, bool)> onRailBuildSE_ = nullptr;
    };
}

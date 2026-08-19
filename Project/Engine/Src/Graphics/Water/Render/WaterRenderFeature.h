#pragma once

#include "Scene/Feature/ISceneFeature.h"
#include "Graphics/Water/Simulation/WaterSurfaceModelProvider.h"
#include "Graphics/Water/Simulation/WaterSurfaceSimulator.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector3.h"

#include <memory>

class SkyBoxObject;

namespace CoreEngine
{
    class WaterPlaneObject;
    class RenderDomainContext;

    /// @brief 水面描画一式（水面オブジェクト・波シミュレーション・外部リソース結線）を持つ Feature
    /// @details AddFeature() するだけで水面が成立する。結線を PostLogic で行うのは、
    ///          大気散乱（EnvironmentFeature）の更新後でないと LUT / SH の準備完了が確定しないため。
    class WaterRenderFeature : public ISceneFeature {
    public:
        /// @brief 水面オブジェクトの生成パラメータ
        struct Config {
            /// @brief メッシュのローカル一辺長 [m]
            float size = 100.0f;
            /// @brief XZ 分割数
            /// @details FFT カスケードの波形をジオメトリで解像するには頂点間隔が
            ///          カスケード波長より十分細かい必要がある。
            ///          ローカル 100m × 分割 256 × スケール 40 = 一辺 4km / 頂点間隔 15.6m。
            ///          スケールを上げすぎると頂点間隔が波長を超え「平面全体が上下するだけ」に退化する。
            uint32_t resolution = 256;
            /// @brief FFT Ocean 描画経路で開始するか
            bool useFFTOcean = true;
            /// @brief ワールド位置（既定の無限遠タイル床 y=0 に合わせる）
            Vector3 translate{ 0.0f, 0.0f, 0.0f };
            /// @brief ワールドスケール
            Vector3 scale{ 40.0f, 1.0f, 40.0f };
        };

        explicit WaterRenderFeature(const Config& config = {});
        ~WaterRenderFeature() override;

        const char* GetName() const override { return "WaterRender"; }

        void Initialize(SceneContext& ctx) override;
        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;
        void Finalize(SceneContext& ctx) override;

        /// @brief 管理中の水面オブジェクトを返す（未生成なら nullptr）
        WaterPlaneObject* GetWaterPlane() const { return waterPlane_; }

        /// @brief 現在の水面高さ（ワールド Y）を返す
        float GetWaterHeight() const;

        /// @brief 現在フレームの水面状態を返す（水面が無効なら regionValid=0）
        const WaterSurfaceData& GetWaterSurfaceState() const { return waterSurfaceState_; }

        /// @brief 水面状態を即座に再構築する
        /// @details UI で波パラメータを変更した直後など、次フレームを待たずに
        ///          RT 側へ反映したい場合に呼ぶ。
        void RefreshWaterSurfaceState();

    private:
        /// @brief 現在の描画経路に対応する simulator を返す
        WaterSurfaceSimulator* GetActiveSimulator() const;

        /// @brief 水面オブジェクトをシーンから採用、無ければ生成する
        void AcquireWaterPlane(SceneContext& ctx);

        /// @brief 生成直後の水面マテリアル既定値を設定する
        void ConfigureDefaultMaterial() const;

        /// @brief RT 屈折・反射・コースティクスへ surface provider を接続する
        void ConnectSurfaceModelProvider(RenderDomainContext& domain) const;

        /// @brief このフレームの外部リソース結線を組み立てる
        /// @brief WaterCVars（単一情報源）から全水面設定を各所へ反映する（毎フレーム）
        /// @details 見た目/水質/泡 → WaterPlaneObject、FFT → FFTOceanManager（revision 変化時のみ）、
        ///          コースティクス → Technique + RT設定、DXR屈折 → RT設定。
        ///          UI（CVarツリー / 水面パネル）はストレージを書くだけで、適用は必ずここを通る。
        void ApplySettingsFromCVars(SceneContext& ctx, RenderDomainContext& domain);

        WaterFrameBinding BuildFrameBinding(SceneContext& ctx, RenderDomainContext& domain) const;

        /// @brief 水面描画と同じ σa を RT コースティクスへ同期する
        void SyncCausticsAbsorption(RenderDomainContext& domain) const;

        /// @brief 泡パラメータ（WaterFrameConstants が単一情報源）を泡蓄積パスへ同期する
        void SyncFoamSettings(RenderDomainContext& domain) const;

        /// @brief 結線結果の診断ログ（デバッグ表示中のみ・低頻度）
        /// @details 以前は WaterPlaneObject の setter 6 個と BindCustomResources に
        ///          散っていたログをここへ集約した。
        void LogFrameDiagnostics(const WaterFrameBinding& binding) const;

        Config config_{};

        /// @brief 水面描画本体（所有権は GameObjectManager）
        WaterPlaneObject* waterPlane_ = nullptr;

        /// @brief 空気遠近感の適用可否判定に使う空（所有権は GameObjectManager）
        SkyBoxObject* skyBox_ = nullptr;

        /// @brief 現在フレームの水面状態（RenderDomainContext へ publish する実体）
        WaterSurfaceData waterSurfaceState_{};

        /// @brief RT 水面パスへ渡す surface model provider（寿命管理付き）
        std::shared_ptr<StaticWaterSurfaceModelProvider> surfaceModelProvider_{};

        /// @brief Gerstner Wave 用 simulator
        std::unique_ptr<WaterSurfaceSimulator> gerstnerSimulator_{};

        /// @brief FFT Ocean 用 simulator
        std::unique_ptr<WaterSurfaceSimulator> fftOceanSimulator_{};

        /// @brief publish 先（Finalize で publish を取り消すために保持。非所有）
        RenderDomainContext* publishTarget_ = nullptr;

        // FFT 関連 CVar の revision 合計。変化したときだけ FFTOceanManager::SetSettings を
        // 呼ぶ（SetSettings は WaitForGpuIdle＋スペクトル再構築を伴うため毎フレーム
        // 無条件で呼んではいけない。サニタイズ（風向正規化）で CVar 値と保持値が
        // 恒常的に食い違い、毎フレーム再構築になる事故も防ぐ）。~0u は初回強制適用。
        uint32_t lastFFTCVarRevisionSum_ = ~0u;

        /// @brief 直近にログした白波被覆率の風速追従係数（変化時のみログするため）
        float lastFoamWindCoverageScale_ = -1.0f;
    };
}

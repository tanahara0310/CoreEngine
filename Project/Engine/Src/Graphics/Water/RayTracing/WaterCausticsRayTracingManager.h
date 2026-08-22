#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include <memory>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector3.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorManager;
    class AccelerationStructureManager;

    /// @brief RT コースティクスの設定
    struct WaterCausticsRayTracingSettings {
        float maxTraceDistance = 500.0f;
        float surfaceBias = 0.02f;
        // 1.0 = 物理値（水平太陽照度 × ヤコビアン集光率）をそのまま出力する。
        // 旧実装（集光項なし・可視性テストのみ）の見た目合わせだった 3.0 から変更。
        float intensityScale = 1.0f;
        float refractiveIndex = 1.333f;
        // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画（Jerlov プリセット/濁度 UI）と
        // 同じ値が WaterRenderFeature から毎フレーム同期される。
        // 既定値は Ocean プリセット（Jerlov I 相当）と同値。
        float absorptionCoeff[3] = { 0.35f, 0.07f, 0.02f };
        float debugDisplayScale = 1.0f;
        uint32_t debugViewMode = 0;
        uint32_t debugLogEnabled = 0;
    };

    /// @brief 水面のコースティクスをレイトレーシングで生成するマネージャ
    class WaterCausticsRayTracingManager : public WaterRayTracingPassBase {
    public:
        // シーンの実際のディレクショナルライト情報。
        // 以前は方向のみを受け取っており、色・強度・有効フラグが RT コースティクスへ
        // 一切反映されていなかった（ライトを消してもコースティクスが消えない、
        // ライト色を変えても常に白いままになる不具合の原因）。
        struct LightInput {
            Vector3 direction{ 0.0f, -1.0f, 0.0f };
            Vector3 color{ 1.0f, 1.0f, 1.0f };
            float intensity = 1.0f;
            bool enabled = true;
        };

        // ビュー識別子は 3 マネージャ共通（WaterRayTracingPassBase.h の RTWaterViewID）
        using ViewID = RTWaterViewID;

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static_assert(kViewCount <= RayTracingOutputViewSet::kMaxSlotCount,
            "WaterCausticsRayTracingManager: ViewID::Count exceeds RayTracingOutputViewSet::kMaxSlotCount");

        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

        /// @param sceneDepthSRV WorldPosition ターゲット廃止に伴い深度から復元する
        /// @param invViewProj 深度復元用 View*Projection の逆行列
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const LightInput& lightInput,
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput,
            const Matrix4x4& invViewProj,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        /// @brief 出力テクスチャを指定サイズで作り直す
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        /// @brief コースティクス出力テクスチャの SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetCausticsSRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief コースティクス出力テクスチャのリソース
        ID3D12Resource* GetCausticsResource(ViewID viewId = ViewID::GameView) const;
        /// @brief コースティクス出力の現在ステートへの参照（バリア時に更新される）
        D3D12_RESOURCE_STATES& GetCausticsCurrentState(ViewID viewId = ViewID::GameView);

        void SetSettings(const WaterCausticsRayTracingSettings& settings) { settings_ = settings; }
        const WaterCausticsRayTracingSettings& GetSettings() const { return settings_; }

    private:
        WaterCausticsRayTracingSettings settings_{};
    };
}

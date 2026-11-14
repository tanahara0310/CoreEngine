#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/Fog/Volumetric/VolumetricFog.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Graphics/RayTracing/RayTracingDispatchInfo.h"
#include "Graphics/RayTracing/RayTracingHitData.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"

namespace CoreEngine
{
    class GraphicsCore;
    class AccelerationStructureManager;
    class LightManager;
    struct Light;
    struct ViewInfo;

    /// @brief ボリュメトリックフォグの物の影に使う、太陽から見た深さの画像
    /// @details 霧の格子が届く範囲を包む球を光に垂直な面へ写し、その面に並べた点から光の進む向きへ
    ///          レイを撃って、最初に当たった距離を R32_FLOAT の画像へ書く。霧の格子（LightScattering）は
    ///          点の距離と画像の距離を比べて影を決める。毎フレーム Trace を呼ぶ。
    ///          r.Fog.Volumetric.ShadowUpdateInterval のフレームごとに撮り直し、間のフレームは前の画像と座標を使う。
    class RTVolumetricShadow : public RTEffect {
    public:
        bool Initialize(const RTEffectInitContext& init) override;
        void BeginFrame() override;

        /// @brief パイプラインが使える状態か
        bool IsInitialized() const { return initialized_; }

        /// @brief 霧に物の影を落とす平行光源を選ぶ
        /// @details 点いていて「影を落とす」「霧に影を落とす」が立ち、霧を照らす平行光源のうち、
        ///          実効の明るさ（大気の透過率込みの色の輝度 × 強度 × 霧を照らす強さ）がいちばん大きい 1 灯
        /// @return 無ければ nullptr
        static const Light* SelectLight(const LightManager& lights);

        /// @brief レイを撃って画像を書く
        /// @param view        霧の格子を作るビュー
        /// @param fogDistance 霧の格子が届く距離 [m]
        /// @param hitTables   材質の表（そろわなければアルファを調べない）
        void Trace(ID3D12GraphicsCommandList* cmdList,
            const ViewInfo& view,
            const LightManager& lights,
            float fogDistance,
            const RTHitTableIndices& hitTables);

        /// @brief 今フレームに画像を書いたか
        bool IsDispatchedThisFrame() const { return dispatchedThisFrame_; }

        /// @brief 今フレームに使える画像があるか（今フレームに書いたか、前に書いた画像を使い回す）
        bool IsAvailableThisFrame() const { return availableThisFrame_; }

        /// @brief 今フレームに使う画像と座標（IsAvailableThisFrame のときだけ有効。使い回すフレームは書いたときの座標）
        const VolumetricFogSunDepth& GetFrame() const { return frame_; }

        /// @brief 画像（ステート追跡込み）と SRV
        GpuResource& GetDepth() { return texture_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSrv() const { return srv_.gpuHandle; }

        /// @brief 直近のディスパッチの診断情報
        const RayTracingDispatchInfo& GetDispatchInfo() const { return dispatchInfo_; }

    private:
        /// @brief 一辺 resolution の画像を（大きさが変わったら作り直して）用意する
        bool EnsureTexture(uint32_t resolution);

        /// @brief 画像を GPU が使い終わってから手放す
        void RetireTexture();

        /// @brief 前に書いた画像を今フレームも使えるか
        /// @param center 今フレームの霧の範囲を包む球の中心 [m]
        /// @param radius 今フレームの霧の範囲を包む球の半径 [m]
        bool CanReuseFrame(const Light* light, uint32_t resolution, const Vector3& center, float radius) const;

        GraphicsCore* dxCommon_ = nullptr;
        AccelerationStructureManager* asMgr_ = nullptr;

        RootSignatureManager rootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        BindingTable bindings_;
        bool initialized_ = false;

        GpuResource texture_;
        DescriptorHandle srv_{};
        DescriptorHandle uav_{};
        uint32_t resolution_ = 0;

        VolumetricFogSunDepth frame_{};
        RayTracingDispatchInfo dispatchInfo_{};
        bool dispatchedThisFrame_ = false;
        bool availableThisFrame_ = false;
        bool tracedCalledThisFrame_ = false; ///< 今フレームに Trace が呼ばれたか
        bool tracedCalledLastFrame_ = false; ///< 前のフレームに Trace が呼ばれたか
        bool frameValid_ = false;            ///< frame_ が使い回せる画像を指しているか
        uint32_t framesSinceTrace_ = 0;      ///< 最後に書いてから使い回したフレームの数
        Vector3 tracedCenter_{};             ///< 書いたときの霧の範囲を包む球の中心 [m]
        float tracedRadius_ = 0.0f;          ///< 書いたときの霧の範囲を包む球の半径 [m]
    };
}

#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/MathCore.h"

#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{
    class AtmosphereManager;
    class GraphicsCore;
    class LightManager;
    struct FogSettings;
    struct ViewInfo;

    /// @brief 今フレームの格子を作るのに要る値
    struct VolumetricFogFrameInput {
        const ViewInfo* view = nullptr;
        uint32_t screenWidth = 0;               ///< SceneColor の幅 [px]
        uint32_t screenHeight = 0;              ///< SceneColor の高さ [px]
        const LightManager* lightManager = nullptr;
        const AtmosphereManager* atmosphere = nullptr;
        const FogSettings* fog = nullptr;       ///< 高さフォグの濃さ
        Vector3 fallbackAmbient{};              ///< 空の SH を使わないときの空の光（輝度）
    };

    /// @brief ボリュメトリックフォグの格子（画面に揃えた 3D の配列）と、それを作る 3 本の CS
    class VolumetricFog {
    public:
        /// @brief 格子を照らす平行光源の最大数（VolumetricFogCommon.hlsli と一致）
        static constexpr uint32_t kMaxDirectionalLights = 4;

        /// @brief 格子を照らす平行光源 1 灯（VolumetricFogCommon.hlsli の VolumetricFogDirectionalLight と 1 対 1）
        struct DirectionalLight {
            Vector3 direction;  ///< 光の進行方向（正規化済み）
            float   pad0;
            Vector3 irradiance; ///< 照度（色 × 強度。シェーダー単位）
            float   pad1;
        };

        /// @brief 格子の定数（VolumetricFogCommon.hlsli の VolumetricFogParameters と 1 対 1）
        struct Constants {
            Matrix4x4 invViewProj;
            Vector3   cameraWorldPos;
            float     distance;
            uint32_t  gridSize[3];
            float     depthDistribution;
            Vector2   screenToGrid;
            Vector2   gridPixelSize;
            Vector3   albedo;
            float     phaseG;
            float     density;
            float     heightFalloff;
            float     heightRef;
            float     startDistance;
            Vector3   fallbackAmbient;
            uint32_t  skyAmbientEnabled;
            float     skyAmbientScale;
            uint32_t  directionalCount;
            uint32_t  debugView;
            float     pad0;
            DirectionalLight directional[kMaxDirectionalLights];
        };

        static constexpr Cb::Field kConstantsFields[] = {
            CB_FIELD(Constants, invViewProj),       CB_FIELD(Constants, cameraWorldPos),
            CB_FIELD(Constants, distance),          CB_FIELD(Constants, gridSize),
            CB_FIELD(Constants, depthDistribution), CB_FIELD(Constants, screenToGrid),
            CB_FIELD(Constants, gridPixelSize),     CB_FIELD(Constants, albedo),
            CB_FIELD(Constants, phaseG),            CB_FIELD(Constants, density),
            CB_FIELD(Constants, heightFalloff),     CB_FIELD(Constants, heightRef),
            CB_FIELD(Constants, startDistance),     CB_FIELD(Constants, fallbackAmbient),
            CB_FIELD(Constants, skyAmbientEnabled), CB_FIELD(Constants, skyAmbientScale),
            CB_FIELD(Constants, directionalCount),  CB_FIELD(Constants, debugView),
            CB_FIELD(Constants, pad0),
            CB_FIELD(Constants, directional),
        };
        CB_VERIFY_LAYOUT(Constants, kConstantsFields);
        CB_BIND_HLSL(Constants, kConstantsFields, "gVolumetricFog");

    public:
        /// @brief 3 本の CS のパイプラインを作る
        /// @return 3 本とも作れたら true
        bool Initialize(GraphicsCore* graphicsCore);

        /// @brief 3 本の CS のパイプラインが作れているか
        bool IsPipelineReady() const { return pipelineReady_; }

        /// @brief 今フレームの格子の資源を（大きさが変わったら作り直して）用意し、定数を確保する
        /// @return 格子を作ってよいなら true
        bool PrepareFrame(const VolumetricFogFrameInput& input);

        /// @brief 散乱係数と消散係数を書く
        void DispatchInject(ID3D12GraphicsCommandList* cmdList);

        /// @brief 視線の向きへ散乱する光を書く
        void DispatchLightScattering(ID3D12GraphicsCommandList* cmdList);

        /// @brief 手前から奥へ積み上げる
        void DispatchIntegrate(ID3D12GraphicsCommandList* cmdList);

        /// @brief 今フレームの積み上げが終わっているか
        bool IsFrameReady() const { return integratedThisFrame_; }

        /// @brief 今フレームの定数の GPU 仮想アドレス（PrepareFrame の前は 0）
        D3D12_GPU_VIRTUAL_ADDRESS GetFrameConstantsAddress() const { return frameConstants_; }

        /// @brief 空の SH の SRV（大気が無ければ 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSkyIrradianceSrv() const { return skyIrradianceSrv_; }

        /// @brief 積み上げた格子の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetIntegratedSrv() const { return integrated_.srv.gpuHandle; }

        /// @brief 積み上げた格子（ステート追跡込み）
        GpuResource& GetIntegratedResource() { return integrated_.texture; }

        /// @brief 今フレームの格子の数（PrepareFrame の後に有効）
        const uint32_t* GetGridSize() const { return gridSize_; }

        /// @brief フレームの終わりに今フレームの状態を落とす
        void EndFrame();

    private:
        /// @brief 3D の画像 1 枚と SRV・UAV
        struct Volume {
            GpuResource texture;
            DescriptorHandle srv{};
            DescriptorHandle uav{};
        };

        /// @brief 格子の数が変わったら 3 枚の画像を作り直す
        bool EnsureVolumes(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);

        /// @brief 3D の画像 1 枚を作る
        bool CreateVolume(Volume& volume, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, const char* name);

        /// @brief 3D の画像 1 枚を GPU が使い終わってから手放す
        void RetireVolume(Volume& volume);

        /// @brief 平行光源を定数へ詰める
        void FillDirectionalLights(Constants& constants, const LightManager* lightManager) const;

        GraphicsCore* graphicsCore_ = nullptr;

        CustomShaderPipeline injectPipeline_{};
        CustomShaderPipeline lightScatteringPipeline_{};
        CustomShaderPipeline integratePipeline_{};
        BindingTable injectBindings_{};
        BindingTable lightScatteringBindings_{};
        BindingTable integrateBindings_{};
        bool pipelineReady_ = false;

        Volume vBuffer_{};
        Volume lightScattering_{};
        Volume integrated_{};
        uint32_t gridSize_[3]{};

        D3D12_GPU_VIRTUAL_ADDRESS frameConstants_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSrv_{};
        bool integratedThisFrame_ = false;
    };
}

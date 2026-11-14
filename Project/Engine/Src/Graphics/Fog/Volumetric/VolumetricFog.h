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
    struct CloudShadowShaderConstants;
    struct FogSettings;
    struct Light;
    struct ViewInfo;

    /// @brief 太陽から見た深さの画像（光に垂直な面から光の進む向きへ測った、最初に当たった距離 [m]）
    struct VolumetricFogSunDepth {
        const Light* light = nullptr;           ///< 画像を作った平行光源（見分けるだけで中身は読まない）
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};      ///< 画像の SRV（R32_FLOAT）
        Vector3 origin{};                       ///< 面の中心（レイの始点の面）[m]
        Vector3 right{};                        ///< 面の右の向き
        Vector3 up{};                           ///< 面の上の向き
        Vector3 direction{};                    ///< レイの向き（光の進行方向）
        float size = 0.0f;                      ///< 面の一辺 [m]
        float range = 0.0f;                     ///< レイの長さ [m]
        float bias = 0.0f;                      ///< 日なたとみなす距離の余裕 [m]
    };

    /// @brief 今フレームの格子を作るのに要る値
    struct VolumetricFogFrameInput {
        const ViewInfo* view = nullptr;
        uint32_t screenWidth = 0;               ///< SceneColor の幅 [px]
        uint32_t screenHeight = 0;              ///< SceneColor の高さ [px]
        const LightManager* lightManager = nullptr;
        const AtmosphereManager* atmosphere = nullptr;
        const FogSettings* fog = nullptr;       ///< 高さフォグの濃さ
        Vector3 fallbackAmbient{};              ///< 空の SH を使わないときの空の光（輝度）
        D3D12_GPU_DESCRIPTOR_HANDLE cloudShadowMapSrv{};          ///< 雲の影のマップ（雲が無ければ 0）
        const CloudShadowShaderConstants* cloudShadow = nullptr; ///< 雲の影のマップの範囲と強さ（雲が無ければ nullptr）
        const VolumetricFogSunDepth* sunDepth = nullptr;         ///< 太陽から見た深さの画像（今フレームに作っていなければ nullptr）
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
            Matrix4x4 prevViewProj;
            Vector3   prevCameraWorldPos;
            float     historyWeight;
            Vector3   frameJitter;
            uint32_t  historyValid;
            Vector2   cloudShadowRegionCenterXZ;
            float     cloudShadowRegionSize;
            float     cloudShadowAnchorY;
            float     cloudShadowEdgeFadeStart;
            float     cloudShadowStrength;
            uint32_t  cloudShadowEnabled;
            float     pad1;
            Vector3   sunDepthOrigin;
            float     sunDepthInvSize;
            Vector3   sunDepthRight;
            float     sunDepthBias;
            Vector3   sunDepthUp;
            float     sunDepthRange;
            Vector3   sunDepthDir;
            uint32_t  sunDepthLightIndex;
            DirectionalLight directional[kMaxDirectionalLights];
        };

        /// @brief 前方描画と全画面の合成が格子を引くのに要る値（FogParameters の volumetric* へ写す）
        struct ForwardParams {
            Matrix4x4 viewProj{};           ///< 格子を作ったジッタ無しの View*Projection
            Vector3   gridSize{};           ///< 格子の数（横・縦・奥行き）
            float     distance = 0.0f;      ///< 格子が届く距離 [m]
            Vector2   screenToGrid{};       ///< 画面の UV → 格子の UV の倍率
            float     depthDistribution = 0.0f; ///< 奥ほど厚くする度合い
        };

        /// @brief 太陽から見た深さの画像を使う光源が無いときの番号（VolumetricFogCommon.hlsli と一致）
        static constexpr uint32_t kNoSunDepthLight = 0xFFFFFFFFu;

        /// @brief 格子を照らす点光源・スポット・エリアの最大数（VolumetricFogCommon.hlsli と一致）
        static constexpr uint32_t kMaxLocalLights = 40;

        /// @brief 局所のライトの種類（VolumetricFogCommon.hlsli の kVolumetricFogLocalLight* と一致）
        enum LocalLightType : uint32_t {
            kLocalLightPoint = 0,
            kLocalLightSpot = 1,
            kLocalLightArea = 2,
        };

        /// @brief 格子を照らす点光源・スポット・エリア 1 灯（VolumetricFogCommon.hlsli の VolumetricFogLocalLight と 1 対 1）
        struct LocalLight {
            Vector3  position;   ///< 光源の位置（エリアは発光面の中心）[m]
            float    range;      ///< 届く距離 [m]
            Vector3  intensity;  ///< 色 × 強度 × 霧を照らす強さ（シェーダー単位。1/d² を掛けると照度）
            uint32_t type;       ///< LocalLightType
            Vector3  direction;  ///< スポットの向き・エリアの発光面の法線（正規化済み）
            float    cosOuter;   ///< スポットの外角の cos
            Vector3  right;      ///< エリアの発光面の右の向き
            float    cosInner;   ///< スポットの内角の cos
            Vector3  up;         ///< エリアの発光面の上の向き
            float    halfWidth;  ///< エリアの発光面の幅の半分 [m]
            float    halfHeight; ///< エリアの発光面の高さの半分 [m]
            Vector3  pad;
        };

        static constexpr Cb::Field kLocalLightFields[] = {
            CB_FIELD(LocalLight, position),  CB_FIELD(LocalLight, range),
            CB_FIELD(LocalLight, intensity), CB_FIELD(LocalLight, type),
            CB_FIELD(LocalLight, direction), CB_FIELD(LocalLight, cosOuter),
            CB_FIELD(LocalLight, right),     CB_FIELD(LocalLight, cosInner),
            CB_FIELD(LocalLight, up),        CB_FIELD(LocalLight, halfWidth),
            CB_FIELD(LocalLight, halfHeight), CB_FIELD(LocalLight, pad),
        };
        CB_VERIFY_LAYOUT(LocalLight, kLocalLightFields);

        /// @brief 格子を照らす点光源・スポット・エリアの定数（VolumetricFogCommon.hlsli の VolumetricFogLocalLights と 1 対 1）
        struct LocalLightConstants {
            uint32_t   count;
            uint32_t   pad[3];
            LocalLight lights[kMaxLocalLights];
        };

        static constexpr Cb::Field kLocalLightConstantsFields[] = {
            CB_FIELD(LocalLightConstants, count), CB_FIELD(LocalLightConstants, pad),
            CB_FIELD(LocalLightConstants, lights),
        };
        CB_VERIFY_LAYOUT(LocalLightConstants, kLocalLightConstantsFields);
        CB_BIND_HLSL(LocalLightConstants, kLocalLightConstantsFields, "gVolumetricFogLocalLights");

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
            CB_FIELD(Constants, pad0),              CB_FIELD(Constants, prevViewProj),
            CB_FIELD(Constants, prevCameraWorldPos), CB_FIELD(Constants, historyWeight),
            CB_FIELD(Constants, frameJitter),       CB_FIELD(Constants, historyValid),
            CB_FIELD(Constants, cloudShadowRegionCenterXZ), CB_FIELD(Constants, cloudShadowRegionSize),
            CB_FIELD(Constants, cloudShadowAnchorY), CB_FIELD(Constants, cloudShadowEdgeFadeStart),
            CB_FIELD(Constants, cloudShadowStrength), CB_FIELD(Constants, cloudShadowEnabled),
            CB_FIELD(Constants, pad1),
            CB_FIELD(Constants, sunDepthOrigin),    CB_FIELD(Constants, sunDepthInvSize),
            CB_FIELD(Constants, sunDepthRight),     CB_FIELD(Constants, sunDepthBias),
            CB_FIELD(Constants, sunDepthUp),        CB_FIELD(Constants, sunDepthRange),
            CB_FIELD(Constants, sunDepthDir),       CB_FIELD(Constants, sunDepthLightIndex),
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

        /// @brief 雲の影のマップの SRV（今フレームに雲の影を引かないなら 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetCloudShadowSrv() const { return cloudShadowSrv_; }

        /// @brief 太陽から見た深さの画像の SRV（今フレームに物の影を引かないなら 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSunDepthSrv() const { return sunDepthSrv_; }

        /// @brief 積み上げた格子の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetIntegratedSrv() const { return integrated_.srv.gpuHandle; }

        /// @brief 積み上げた格子（ステート追跡込み）
        GpuResource& GetIntegratedResource() { return integrated_.texture; }

        /// @brief 格子の奥の光の画像の SRV（縦横は格子と同じ・rgb = アルベド × 格子の端の点の光の源）
        D3D12_GPU_DESCRIPTOR_HANDLE GetFarLightSrv() const { return farLight_.srv.gpuHandle; }

        /// @brief 最後に作った格子を引くのに要る値（PrepareFrame の後に有効）
        const ForwardParams& GetForwardParams() const { return forwardParams_; }

        /// @brief 今フレームの格子の数（PrepareFrame の後に有効）
        const uint32_t* GetGridSize() const { return gridSize_; }

        /// @brief 最後に作った格子を照らした点光源・スポット・エリアの数
        uint32_t GetLocalLightCount() const { return localLightCount_; }

        /// @brief フレームの終わりに今フレームの状態を落とす
        void EndFrame();

    private:
        /// @brief 画像 1 枚と SRV・UAV
        struct Volume {
            GpuResource texture;
            DescriptorHandle srv{};
            DescriptorHandle uav{};
        };

        /// @brief 格子の数が変わったら 4 枚の 3D の画像と奥の光の画像を作り直す（作り直したら前フレームの光は捨てる）
        bool EnsureVolumes(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);

        /// @brief 雲の影の値を定数へ詰め、マップの SRV を覚える
        void FillCloudShadow(Constants& constants, const VolumetricFogFrameInput& input);

        /// @brief 太陽から見た深さの画像の座標を定数へ詰め、画像の SRV を覚える（FillDirectionalLights の後に呼ぶ）
        void FillSunDepth(Constants& constants, const VolumetricFogFrameInput& input);

        /// @brief 前フレームの光を混ぜてよいか、混ぜるならどこへずらすかを定数へ詰める
        void FillHistory(Constants& constants, const Matrix4x4& viewProj, const Vector3& cameraPos);

        /// @brief 3D の画像 1 枚を作る
        bool CreateVolume(Volume& volume, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, const char* name);

        /// @brief 2D の画像 1 枚を作る
        bool CreateImage(Volume& image, uint32_t sizeX, uint32_t sizeY, const char* name);

        /// @brief 画像 1 枚と SRV・UAV を作る（3D と 2D）
        bool CreateTexture(Volume& volume, const D3D12_RESOURCE_DESC& desc, const char* name);

        /// @brief 3D の画像 1 枚を GPU が使い終わってから手放す
        void RetireVolume(Volume& volume);

        /// @brief 平行光源を定数へ詰める
        /// @param sunDepthLight 太陽から見た深さの画像を作った光源（無ければ nullptr）。詰めた番号を sunDepthLightIndex へ書く
        void FillDirectionalLights(
            Constants& constants, const LightManager* lightManager, const Light* sunDepthLight) const;

        /// @brief 届く範囲が格子にかかる点光源・スポット・エリアを詰めて、今フレームの定数を確保する
        /// @param cameraPos カメラのワールド座標 [m]
        /// @param distance 格子が届く距離 [m]
        /// @return 確保できたら true
        bool AllocateLocalLights(const LightManager* lightManager, const Vector3& cameraPos, float distance);

        GraphicsCore* graphicsCore_ = nullptr;

        CustomShaderPipeline injectPipeline_{};
        CustomShaderPipeline lightScatteringPipeline_{};
        CustomShaderPipeline integratePipeline_{};
        BindingTable injectBindings_{};
        BindingTable lightScatteringBindings_{};
        BindingTable integrateBindings_{};
        bool pipelineReady_ = false;

        Volume vBuffer_{};
        Volume lightScattering_[2]{}; ///< 今フレームと前フレームの光を交互に使う
        Volume integrated_{};
        Volume farLight_{};
        uint32_t gridSize_[3]{};
        ForwardParams forwardParams_{};

        D3D12_GPU_VIRTUAL_ADDRESS frameConstants_ = 0;
        D3D12_GPU_VIRTUAL_ADDRESS localLightConstants_ = 0;
        uint32_t localLightCount_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSrv_{};
        D3D12_GPU_DESCRIPTOR_HANDLE cloudShadowSrv_{};
        D3D12_GPU_DESCRIPTOR_HANDLE sunDepthSrv_{};
        bool integratedThisFrame_ = false;

        /// @brief 今フレームに光を書く方の lightScattering_ の番号
        uint32_t currentLight_ = 0;
        /// @brief 格子を作った回数（ずらし方の列の番号）
        uint32_t frameIndex_ = 0;

        /// @brief 前フレームに光を書き終えたか（書いていなければ混ぜない）
        bool historyAvailable_ = false;
        Matrix4x4 prevViewProj_{};
        Vector3 prevCameraPos_{};
        float prevDistance_ = 0.0f;
        float prevDepthDistribution_ = 0.0f;
        /// @brief 前フレームに太陽から見た深さの画像を作った光源（見分けるだけで中身は読まない）
        const Light* prevSunDepthLight_ = nullptr;
    };
}

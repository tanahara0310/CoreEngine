#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <memory>

#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "WaterRayTracingPassBase.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief RT 反射の設定
    struct WaterReflectionRayTracingSettings {
        float maxRayDistance = 2000.0f;
        float surfaceBias = 0.05f;
        // 反射ヒット点のスクリーン再投影ずれ量の上限（ピクセル）。
        // 0 = 無制限（RT で求めた正確な位置をそのまま使う。既定）。
        float maxReflectionOffsetPixels = 0.0f;
        float debugDisplayScale = 1.0f;
        uint32_t debugViewMode = 0;
        uint32_t debugLogEnabled = 0;
    };

    /// @brief 水面の点から光源が見えるかを調べるための光源
    struct WaterSunShadowInput {
        Vector3 direction{ 0.0f, -1.0f, 0.0f }; ///< 光源→シーン
        bool enabled = false;                  ///< false なら全画素を日向（1）にする
    };

    /// @brief 反射レイが当たった点を照らすための入力（番号はすべてシェーダー可視ヒープ内のインデックス）
    struct WaterHitShadingInput {
        bool enabled = false;                         ///< false なら当たった点を照らさない（画面に無い分は空）
        uint32_t instanceTableIndex = UINT32_MAX;     ///< RTHitInstance の表
        uint32_t subMeshTableIndex = UINT32_MAX;      ///< RTHitSubMesh の表
        uint32_t directionalLightsIndex = UINT32_MAX; ///< DirectionalLightData の配列
        uint32_t directionalLightCount = 0;
        uint32_t skyIrradianceSHIndex = UINT32_MAX;   ///< 空の放射照度の SH9
        bool skyAmbientEnabled = false;
        float skyAmbientScale = 0.0f;                 ///< 空の輝度単位 → サーフェス光単位
        uint32_t skySpecularMapIndex = UINT32_MAX;    ///< 空のスペキュラキューブマップ
        bool skySpecularEnabled = false;
    };

    /// @brief DXR による水面反射マネージャー。
    /// @details RTWaterRefractionRayTracingManager の対称形。反射レイをトレースし、
    ///          ヒット点が画面に写っていればその色を、写っていなければ当たった点を
    ///          材質と光で照らした色を水面反射カラーにする。
    ///          同じ画素で水面の点から光源へ影のレイも撃ち、日向率（0=影 / 1=日向）を
    ///          2 枚目の出力に書く。
    ///          反射は GameView のみで必要なため ViewID は GameView 1 本。
    class WaterReflectionRayTracingManager : public WaterRayTracingPassBase {
    public:
        // ビュー識別子は 3 マネージャ共通（WaterRayTracingPassBase.h の RTWaterViewID）。
        // 反射が実際に使うのは GameView のみ（出力スロットは使用したビューだけ確保される）。
        using ViewID = RTWaterViewID;

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        /// @brief 日向率テクスチャのスロット（反射出力のスロット 0〜kViewCount-1 の後ろ）
        static constexpr uint32_t kSunVisibilitySlotBase = kViewCount;
        static_assert(kSunVisibilitySlotBase + kViewCount <= RayTracingOutputViewSet::kMaxSlotCount,
            "WaterReflectionRayTracingManager: slot count exceeds RayTracingOutputViewSet::kMaxSlotCount");

        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr,
            ShaderProgramCache* shaderProgramCache);

        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
            const Matrix4x4& viewProjection,
            const Vector3& cameraPosition,
            const WaterSurfaceData& surfaceData,
            const FFTOceanInput& fftOceanInput,
            /// 空キューブマップ（AtmosphereManager::GetSkySpecularSRVHandle）。
            /// ptr==0 なら空を解決せず理由コードを返し、Water.PS の保険が動く。
            D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV,
            const WaterSunShadowInput& sunShadow,
            const WaterHitShadingInput& hitShading,
            UINT width,
            UINT height,
            ViewID viewId = ViewID::GameView);

        /// @brief 出力テクスチャを指定サイズで作り直す
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView);

        /// @brief 反射出力テクスチャの SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetReflectionSRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief 反射出力テクスチャをステート追跡つきで返す（バリア発行はこれを渡す）
        GpuResource& GetReflectionResource(ViewID viewId = ViewID::GameView);

        /// @brief 水面の日向率テクスチャ（R8_UNORM・0=影 / 1=日向）の SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetSunVisibilitySRVHandle(ViewID viewId = ViewID::GameView) const;
        /// @brief 水面の日向率テクスチャをステート追跡つきで返す
        GpuResource& GetSunVisibilityResource(ViewID viewId = ViewID::GameView);

        void SetSettings(const WaterReflectionRayTracingSettings& settings) { settings_ = settings; }
        const WaterReflectionRayTracingSettings& GetSettings() const { return settings_; }

    private:
        WaterReflectionRayTracingSettings settings_{};

        // ---- ヒットシェーディングの定数（b2）----
        // フレームインフライト×ビューぶんの枠を 1 本の UPLOAD バッファに並べ、写像したまま書く
        static constexpr UINT kHitShadingConstantsStride = 256;
        Microsoft::WRL::ComPtr<ID3D12Resource> hitShadingConstants_;
        uint8_t* hitShadingConstantsMapped_ = nullptr;

        /// @brief ヒットシェーディングの定数バッファを作る
        bool InitializeHitShadingConstants();
        /// @brief 今フレーム・このビューの枠へ定数を書き、その GPU アドレスを返す
        D3D12_GPU_VIRTUAL_ADDRESS UploadHitShadingConstants(const WaterHitShadingInput& input, uint32_t viewIndex);

        // ---- 反射の元画像の縮小段（段 0 = 半分の解像度）----
        // 荒れた水面の反射は、法線から外した細かい波の分だけ当たった物をぼかして引く。
        // 色の段は物の画素の色（乗算済みアルファ）、空の段は空の画素の割合を持つ
        static constexpr uint32_t kColorPyramidMaxMips = 6;
        GpuResource colorPyramid_;
        std::array<DescriptorHandle, kColorPyramidMaxMips> colorPyramidMipSrv_{};
        std::array<DescriptorHandle, kColorPyramidMaxMips> colorPyramidMipUav_{};
        DescriptorHandle colorPyramidFullSrv_{};
        GpuResource skyCoveragePyramid_;
        std::array<DescriptorHandle, kColorPyramidMaxMips> skyCoveragePyramidMipSrv_{};
        std::array<DescriptorHandle, kColorPyramidMaxMips> skyCoveragePyramidMipUav_{};
        DescriptorHandle skyCoveragePyramidFullSrv_{};
        UINT colorPyramidWidth_ = 0;
        UINT colorPyramidHeight_ = 0;
        uint32_t colorPyramidMipCount_ = 0;
        RootSignatureManager colorPyramidRootSigMgr_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> colorPyramidPipelineState_;
        BindingTable colorPyramidBindings_;
        bool colorPyramidPipelineReady_ = false;

        /// @brief 縮小段を作るコンピュートパイプラインを用意する
        bool InitializeColorPyramid();
        /// @brief 画面の大きさに合わせて縮小段のテクスチャを用意する（大きさが同じなら何もしない）
        bool EnsureColorPyramid(UINT screenWidth, UINT screenHeight);
        /// @brief 縮小段のテクスチャ 1 枚と、段ごとの SRV / UAV、全段の SRV を作る
        bool CreatePyramidTexture(
            GpuResource& texture,
            std::array<DescriptorHandle, kColorPyramidMaxMips>& mipSrv,
            std::array<DescriptorHandle, kColorPyramidMaxMips>& mipUav,
            DescriptorHandle& fullSrv,
            DXGI_FORMAT format,
            UINT width,
            UINT height,
            uint32_t mipCount,
            const char* debugName);
        /// @brief 反射の元画像から色と空の全段を作り、全段を NON_PIXEL_SHADER_RESOURCE にして終える
        /// @details 段 0 では画素を水面より上の物・空・水域の水面より下に分ける（深度から位置を戻して判定する）
        void BuildColorPyramid(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sourceSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            const Matrix4x4& invViewProjection,
            const WaterSurfaceData& surfaceData);
    };
}

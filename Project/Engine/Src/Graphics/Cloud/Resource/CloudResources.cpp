#include "pch.h"
#include "CloudResources.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <exception>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief UAV 対応テクスチャ（2D/3D）の Desc を作る
        D3D12_RESOURCE_DESC MakeTextureDesc(uint32_t width, uint32_t height, uint32_t depth,
                                            DXGI_FORMAT format)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = (depth > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                         : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = static_cast<UINT16>(std::max(depth, 1u));
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }

        /// @brief SRV/UAV の Desc を Desc の次元から組み立てる
        void MakeViewDescs(const D3D12_RESOURCE_DESC& desc,
                           D3D12_SHADER_RESOURCE_VIEW_DESC& outSrv,
                           D3D12_UNORDERED_ACCESS_VIEW_DESC& outUav)
        {
            outSrv = {};
            outSrv.Format = desc.Format;
            outSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            outUav = {};
            outUav.Format = desc.Format;

            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                outSrv.Texture3D.MipLevels = 1;
                outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                outUav.Texture3D.WSize = desc.DepthOrArraySize;
            } else {
                outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                outSrv.Texture2D.MipLevels = 1;
                outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            }
        }

        /// @brief テクスチャを確保し、SRV/UAV を（既存スロットがあればそこへ）書く
        /// @param needsSrv false なら UAV だけ作る（合成中間のように読まれないもの）
        /// @details 再生成のたびに新規スロットを取るとディスクリプタスロットが漏れるため、
        ///          Ensure 系で既存スロットへ書き直す
        bool CreateTexture(ID3D12Device* device, DescriptorAllocator* allocator,
                           CloudGpuTexture& tex, const D3D12_RESOURCE_DESC& desc,
                           const char* name, bool needsSrv = true)
        {
            Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
            try {
                tex.Reset(
                    ResourceFactory::CreateTextureResource(
                        deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "CloudResources: テクスチャ({})の生成に失敗", name);
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            MakeViewDescs(desc, srvDesc, uavDesc);

            const std::string label = name;
            if (needsSrv) {
                allocator->EnsureSRV(tex.srv, tex.Get(), srvDesc, (label + "SRV").c_str());
            }
            allocator->EnsureUAV(tex.uav, tex.Get(), uavDesc, (label + "UAV").c_str());
            return true;
        }
    }

    bool CloudResources::CreateNoiseTextures(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        constexpr DXGI_FORMAT kNoiseFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        return CreateTexture(device, descriptorAllocator, baseShapeNoise,
                   MakeTextureDesc(kBaseShapeNoiseSize, kBaseShapeNoiseSize, kBaseShapeNoiseSize, kNoiseFormat),
                   "CloudBaseShape")
            && CreateTexture(device, descriptorAllocator, detailNoise,
                   MakeTextureDesc(kDetailNoiseSize, kDetailNoiseSize, kDetailNoiseSize, kNoiseFormat),
                   "CloudDetail")
            && CreateTexture(device, descriptorAllocator, weatherMap,
                   MakeTextureDesc(kWeatherMapSize, kWeatherMapSize, 1, kNoiseFormat),
                   "CloudWeather");
    }

    bool CloudResources::CreateCloudShadowMap(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        // 太陽方向の雲透過率の上面図
        return CreateTexture(device, descriptorAllocator, cloudShadowMap,
            MakeTextureDesc(kCloudShadowMapSize, kCloudShadowMapSize, 1, DXGI_FORMAT_R16_FLOAT),
            "CloudShadowMap");
    }

    bool CloudResources::EnsureFrameTargets(ID3D12Device* device,
                                            DescriptorAllocator* descriptorAllocator,
                                            GraphicsCore* graphicsCore,
                                            GpuResource& sceneColor,
                                            uint32_t resolutionDivisor)
    {
        if (!sceneColor || !device || !descriptorAllocator) {
            return false;
        }

        const D3D12_RESOURCE_DESC sceneDesc = sceneColor.Desc();

        // 0 サイズ（ウィンドウ最小化時など）では確保しない（0 幅テクスチャ生成のクラッシュ回避）
        if (sceneDesc.Width == 0 || sceneDesc.Height == 0) {
            return false;
        }

        const uint64_t div = std::max(resolutionDivisor, 1u);
        const uint32_t halfW = static_cast<uint32_t>((sceneDesc.Width + div - 1) / div);
        const uint32_t halfH = static_cast<uint32_t>((sceneDesc.Height + div - 1) / div);

        // SceneColor と同サイズ・同分割数で確保済みなら再利用する。
        // 半解像度側も見ないと r.Cloud.ResolutionDivisor の変更が反映されない
        // （合成中間は常に SceneColor と同サイズなので、それだけでは判定にならない）
        if (compositeResult && godRayBuffer && cloudBuffer &&
            compositeResult.Desc().Width == sceneDesc.Width &&
            compositeResult.Desc().Height == sceneDesc.Height &&
            cloudBuffer.Desc().Width == halfW &&
            cloudBuffer.Desc().Height == halfH) {
            return true;
        }

        // 作り直す前に投入済みの描画完了を待つ。待たずに解放すると、まだ前フレームの
        // ディスパッチが参照しているテクスチャを落とすことになる
        if (graphicsCore && (cloudBuffer || godRayBuffer || compositeResult)) {
            graphicsCore->WaitForGpuIdle();
        }

        // 半解像度のレイマーチ結果とゴッドレイ結果（同サイズ・同フォーマット）
        const D3D12_RESOURCE_DESC halfDesc =
            MakeTextureDesc(halfW, halfH, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (!CreateTexture(device, descriptorAllocator, cloudBuffer, halfDesc, "CloudBuffer")) {
            return false;
        }
        if (!CreateTexture(device, descriptorAllocator, godRayBuffer, halfDesc, "GodRayBuffer")) {
            return false;
        }

        // 合成用中間テクスチャ（SceneColor と同サイズ・同フォーマット。SRV としては読まない）
        D3D12_RESOURCE_DESC compositeDesc = sceneDesc;
        compositeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (!CreateTexture(device, descriptorAllocator, compositeResult, compositeDesc,
                "CloudComposite", /*needsSrv=*/false)) {
            return false;
        }

        targetsWidth_ = halfW;
        targetsHeight_ = halfH;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "CloudResources: 描画ターゲット確保 ({}x{} / 分割数 {} → 半解像度 {}x{})",
            static_cast<uint32_t>(sceneDesc.Width), sceneDesc.Height,
            static_cast<uint32_t>(div), halfW, halfH);
        return true;
    }
}

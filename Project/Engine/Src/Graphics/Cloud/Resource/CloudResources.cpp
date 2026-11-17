#include "pch.h"
#include "CloudResources.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Math/IntegerMath.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief テクスチャを確保し、SRV/UAV を（既存スロットがあればそこへ）書く
        /// @details 再生成のたびに新規スロットを取るとディスクリプタスロットが漏れるため、
        ///          Ensure 系で既存スロットへ書き直す
        bool CreateTexture(ID3D12Device* device, DescriptorAllocator* allocator,
                           CloudGpuTexture& tex, const D3D12_RESOURCE_DESC& desc, const char* name)
        {
            const std::string label = name;
            if (!UavTexture::Create(device, *allocator, tex, desc, tex.srv, label + "SRV", tex.uav, label + "UAV")) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "CloudResources: テクスチャ({})の生成に失敗", name);
                return false;
            }

            // ミップ生成 CS は「1 段だけを見る SRV」と「1 段だけへ書く UAV」を対で使う
            const uint32_t mipLevels = desc.MipLevels;
            if (mipLevels > 1) {
                tex.mipSrvs.resize(mipLevels);
                tex.mipUavs.resize(mipLevels);
                for (uint32_t mip = 0; mip < mipLevels; ++mip) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC mipSrv{};
                    D3D12_UNORDERED_ACCESS_VIEW_DESC mipUav{};
                    UavTexture::MakeViewDescs(desc, mipSrv, mipUav, mip, 1);
                    const std::string suffix = "Mip" + std::to_string(mip);
                    allocator->EnsureSRV(tex.mipSrvs[mip], tex.Get(), mipSrv,
                        (label + suffix + "SRV").c_str());
                    allocator->EnsureUAV(tex.mipUavs[mip], tex.Get(), mipUav,
                        (label + suffix + "UAV").c_str());
                }
            }
            return true;
        }
    }

    bool CloudResources::CreateNoiseTextures(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        constexpr DXGI_FORMAT kNoiseFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        const bool ok = CreateTexture(device, descriptorAllocator, baseShapeNoise,
                   UavTexture::MakeDesc(kBaseShapeNoiseSize, kBaseShapeNoiseSize, kBaseShapeNoiseSize, kNoiseFormat,
                       UavTexture::ClampMipLevels(kBaseShapeNoiseSize, kNoiseMipLevels)),
                   "CloudBaseShape")
            && CreateTexture(device, descriptorAllocator, detailNoise,
                   UavTexture::MakeDesc(kDetailNoiseSize, kDetailNoiseSize, kDetailNoiseSize, kNoiseFormat,
                       UavTexture::ClampMipLevels(kDetailNoiseSize, kNoiseMipLevels)),
                   "CloudDetail")
            && CreateTexture(device, descriptorAllocator, weatherMap,
                   UavTexture::MakeDesc(kWeatherMapSize, kWeatherMapSize, 1, kNoiseFormat),
                   "CloudWeather");
        if (!ok) {
            return false;
        }

        // 配置ペイントエディタ用: 単一チャンネルをグレースケール複製して表示する SRV
        for (uint32_t ch = 0; ch < 3; ++ch) {
            D3D12_SHADER_RESOURCE_VIEW_DESC channelDesc{};
            channelDesc.Format = kNoiseFormat;
            channelDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            channelDesc.Texture2D.MipLevels = 1;
            channelDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                ch, ch, ch, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
            descriptorAllocator->EnsureSRV(weatherChannelSrvs[ch], weatherMap.Get(), channelDesc,
                ("CloudWeatherCh" + std::to_string(ch) + "SRV").c_str());
        }
        return true;
    }

    bool CloudResources::CreateWeatherPaintTexture(ID3D12Device* device,
                                                   DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        constexpr DXGI_FORMAT kPaintFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (!CreateTexture(device, descriptorAllocator, weatherPaint,
                UavTexture::MakeDesc(kPaintSize, kPaintSize, 1, kPaintFormat), "CloudWeatherPaint")) {
            return false;
        }

        // エディタ表示用: 選んだチャンネルを色、影響度(A)をアルファにして手続き生成マップへ重ねる
        for (uint32_t ch = 0; ch < 3; ++ch) {
            D3D12_SHADER_RESOURCE_VIEW_DESC channelDesc{};
            channelDesc.Format = kPaintFormat;
            channelDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            channelDesc.Texture2D.MipLevels = 1;
            channelDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(ch, ch, ch, 3);
            descriptorAllocator->EnsureSRV(paintChannelSrvs[ch], weatherPaint.Get(), channelDesc,
                ("CloudWeatherPaintCh" + std::to_string(ch) + "SRV").c_str());
        }

        // CPU が書き込むアップロードバッファ（1 行 kPaintSize*4 = 2048B は 256B 境界に乗る）
        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
        weatherPaintUpload = ResourceFactory::CreateBufferResource(deviceRef, kPaintBytes);
        if (!weatherPaintUpload) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "CloudResources: 配置ペイントのアップロードバッファ生成に失敗");
            return false;
        }

        void* mapped = nullptr;
        if (FAILED(weatherPaintUpload->Map(0, nullptr, &mapped))) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "CloudResources: 配置ペイントのアップロードバッファの Map に失敗");
            weatherPaintUpload.Reset();
            return false;
        }
        weatherPaintMapped = static_cast<uint8_t*>(mapped);
        std::memset(weatherPaintMapped, 0, kPaintBytes);
        return true;
    }

    bool CloudResources::CreateCloudShadowMap(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        // 太陽方向の雲透過率の上面図
        return CreateTexture(device, descriptorAllocator, cloudShadowMap,
            UavTexture::MakeDesc(kCloudShadowMapSize, kCloudShadowMapSize, 1, DXGI_FORMAT_R16_FLOAT),
            "CloudShadowMap");
    }

    bool CloudResources::CreateCloudCube(ID3D12Device* device, DescriptorAllocator* descriptorAllocator,
                                         uint32_t faceSize)
    {
        if (!device || !descriptorAllocator || faceSize == 0) {
            return false;
        }

        // 6 面の配列。読み書きとも CloudCubemapCapture.CS だけなので UAV しか作らない
        D3D12_RESOURCE_DESC desc = UavTexture::MakeDesc(faceSize, faceSize, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        desc.DepthOrArraySize = 6;

        if (!UavTexture::CreateResource(device, cloudCube, desc)) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "CloudResources: テクスチャ(CloudCube)の生成に失敗");
            return false;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = 0;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = 6;
        descriptorAllocator->EnsureUAV(cloudCube.uav, cloudCube.Get(), uavDesc, "CloudCubeUAV");
        return true;
    }

    bool CloudResources::EnsureFrameTargets(ID3D12Device* device,
                                            DescriptorAllocator* descriptorAllocator,
                                            GraphicsCore* graphicsCore,
                                            GpuResource& sceneColor,
                                            uint32_t resolutionDivisor,
                                            bool* outRecreated)
    {
        if (outRecreated) {
            *outRecreated = false;
        }
        if (!sceneColor || !device || !descriptorAllocator) {
            return false;
        }

        const D3D12_RESOURCE_DESC sceneDesc = sceneColor.Desc();

        // 0 サイズ（ウィンドウ最小化時など）では確保しない（0 幅テクスチャ生成のクラッシュ回避）
        if (sceneDesc.Width == 0 || sceneDesc.Height == 0) {
            return false;
        }

        const uint64_t div = std::max(resolutionDivisor, 1u);
        const uint32_t halfW = static_cast<uint32_t>(DivRoundUp<uint64_t>(sceneDesc.Width, div));
        const uint32_t halfH = static_cast<uint32_t>(DivRoundUp<uint64_t>(sceneDesc.Height, div));

        // 現在の SceneColor サイズと分割数で確保済みなら再利用する
        if (godRayBuffer && cloudBuffers[0] && cloudBuffers[1] &&
            cloudDepthBuffers[0] && cloudDepthBuffers[1] && traceColor && traceDepth &&
            cloudBuffers[0].Desc().Width == halfW &&
            cloudBuffers[0].Desc().Height == halfH) {
            return true;
        }

        // 作り直す前に投入済みの描画完了を待つ。待たずに解放すると、まだ前フレームの
        // ディスパッチが参照しているテクスチャを落とすことになる
        if (graphicsCore && (cloudBuffers[0] || godRayBuffer)) {
            graphicsCore->WaitForGpuIdle();
        }

        // 半解像度のレイマーチ結果とゴッドレイ結果（同サイズ・同フォーマット）
        const D3D12_RESOURCE_DESC halfDesc =
            UavTexture::MakeDesc(halfW, halfH, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (!CreateTexture(device, descriptorAllocator, cloudBuffers[0], halfDesc, "CloudBuffer0")
            || !CreateTexture(device, descriptorAllocator, cloudBuffers[1], halfDesc, "CloudBuffer1")) {
            return false;
        }
        if (!CreateTexture(device, descriptorAllocator, godRayBuffer, halfDesc, "GodRayBuffer")) {
            return false;
        }

        // 雲の距離と、間引きレイマーチのセルごとの結果。
        // セルの数は間引き方で変わるので、間引かないときの半解像度で確保しておく
        const D3D12_RESOURCE_DESC depthDesc =
            UavTexture::MakeDesc(halfW, halfH, 1, DXGI_FORMAT_R32G32_FLOAT);
        if (!CreateTexture(device, descriptorAllocator, cloudDepthBuffers[0], depthDesc, "CloudDepthBuffer0")
            || !CreateTexture(device, descriptorAllocator, cloudDepthBuffers[1], depthDesc, "CloudDepthBuffer1")
            || !CreateTexture(device, descriptorAllocator, traceColor, halfDesc, "CloudTraceColor")
            || !CreateTexture(device, descriptorAllocator, traceDepth, depthDesc, "CloudTraceDepth")) {
            return false;
        }

        targetsWidth_ = halfW;
        if (outRecreated) {
            *outRecreated = true;
        }
        targetsHeight_ = halfH;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "CloudResources: 描画ターゲット確保 ({}x{} / 分割数 {} → 半解像度 {}x{})",
            static_cast<uint32_t>(sceneDesc.Width), sceneDesc.Height,
            static_cast<uint32_t>(div), halfW, halfH);
        return true;
    }
}

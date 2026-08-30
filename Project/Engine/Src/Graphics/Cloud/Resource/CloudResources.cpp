#include "pch.h"
#include "CloudResources.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief 一辺のサイズから作れるミップ段数を求め、上限で切る
        uint32_t ClampMipLevels(uint32_t smallestExtent, uint32_t desired)
        {
            uint32_t available = 1;
            while ((smallestExtent >> available) >= 1u && available < 16u) {
                ++available;
            }
            return std::max(1u, std::min(desired, available));
        }

        /// @brief UAV 対応テクスチャ（2D/3D）の Desc を作る
        D3D12_RESOURCE_DESC MakeTextureDesc(uint32_t width, uint32_t height, uint32_t depth,
                                            DXGI_FORMAT format, uint32_t mipLevels = 1)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = (depth > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                         : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = static_cast<UINT16>(std::max(depth, 1u));
            desc.MipLevels = static_cast<UINT16>(std::max(mipLevels, 1u));
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }

        /// @brief 指定ミップ段を指す SRV/UAV の Desc を Desc の次元から組み立てる
        /// @param mip 対象のミップ段
        /// @param srvMipCount SRV が見る段数（サンプル用の全段ビューは全体を渡す）
        void MakeViewDescs(const D3D12_RESOURCE_DESC& desc,
                           D3D12_SHADER_RESOURCE_VIEW_DESC& outSrv,
                           D3D12_UNORDERED_ACCESS_VIEW_DESC& outUav,
                           uint32_t mip = 0, uint32_t srvMipCount = 1)
        {
            outSrv = {};
            outSrv.Format = desc.Format;
            outSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            outUav = {};
            outUav.Format = desc.Format;

            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                outSrv.Texture3D.MostDetailedMip = mip;
                outSrv.Texture3D.MipLevels = srvMipCount;
                outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                outUav.Texture3D.MipSlice = mip;
                outUav.Texture3D.WSize = std::max<UINT>(desc.DepthOrArraySize >> mip, 1u);
            } else {
                outSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                outSrv.Texture2D.MostDetailedMip = mip;
                outSrv.Texture2D.MipLevels = srvMipCount;
                outUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                outUav.Texture2D.MipSlice = mip;
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
            const uint32_t mipLevels = desc.MipLevels;
            Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;
            try {
                tex.Reset(
                    ResourceFactory::CreateTextureResource(
                        deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mipLevels);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "CloudResources: テクスチャ({})の生成に失敗", name);
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            MakeViewDescs(desc, srvDesc, uavDesc, 0, mipLevels);

            const std::string label = name;
            if (needsSrv) {
                allocator->EnsureSRV(tex.srv, tex.Get(), srvDesc, (label + "SRV").c_str());
            }
            allocator->EnsureUAV(tex.uav, tex.Get(), uavDesc, (label + "UAV").c_str());

            // ミップ生成 CS は「1 段だけを見る SRV」と「1 段だけへ書く UAV」を対で使う
            if (mipLevels > 1) {
                tex.mipSrvs.resize(mipLevels);
                tex.mipUavs.resize(mipLevels);
                for (uint32_t mip = 0; mip < mipLevels; ++mip) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC mipSrv{};
                    D3D12_UNORDERED_ACCESS_VIEW_DESC mipUav{};
                    MakeViewDescs(desc, mipSrv, mipUav, mip, 1);
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
                   MakeTextureDesc(kBaseShapeNoiseSize, kBaseShapeNoiseSize, kBaseShapeNoiseSize, kNoiseFormat,
                       ClampMipLevels(kBaseShapeNoiseSize, kNoiseMipLevels)),
                   "CloudBaseShape")
            && CreateTexture(device, descriptorAllocator, detailNoise,
                   MakeTextureDesc(kDetailNoiseSize, kDetailNoiseSize, kDetailNoiseSize, kNoiseFormat,
                       ClampMipLevels(kDetailNoiseSize, kNoiseMipLevels)),
                   "CloudDetail")
            && CreateTexture(device, descriptorAllocator, weatherMap,
                   MakeTextureDesc(kWeatherMapSize, kWeatherMapSize, 1, kNoiseFormat),
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
                MakeTextureDesc(kPaintSize, kPaintSize, 1, kPaintFormat), "CloudWeatherPaint")) {
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
            MakeTextureDesc(kCloudShadowMapSize, kCloudShadowMapSize, 1, DXGI_FORMAT_R16_FLOAT),
            "CloudShadowMap");
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
        const uint32_t halfW = static_cast<uint32_t>((sceneDesc.Width + div - 1) / div);
        const uint32_t halfH = static_cast<uint32_t>((sceneDesc.Height + div - 1) / div);

        // 現在の SceneColor サイズと分割数で確保済みなら再利用する
        if (godRayBuffer && cloudBuffers[0] && cloudBuffers[1] &&
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
            MakeTextureDesc(halfW, halfH, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (!CreateTexture(device, descriptorAllocator, cloudBuffers[0], halfDesc, "CloudBuffer0")
            || !CreateTexture(device, descriptorAllocator, cloudBuffers[1], halfDesc, "CloudBuffer1")) {
            return false;
        }
        if (!CreateTexture(device, descriptorAllocator, godRayBuffer, halfDesc, "GodRayBuffer")) {
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

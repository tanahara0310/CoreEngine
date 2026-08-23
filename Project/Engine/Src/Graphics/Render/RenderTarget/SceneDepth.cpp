#include "pch.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

using namespace Microsoft::WRL;

namespace CoreEngine
{
    namespace {
        Logger& logger = Logger::GetInstance();
    }

    SceneDepth::~SceneDepth()
    {
        if (!descriptorAllocator_) {
            return;
        }
        // ハンドルが所有スロットを知っているので、種別の指定も未確保チェックも要らない
        descriptorAllocator_->Free(dsvDescriptor_);
        descriptorAllocator_->Free(depthSRVDescriptor_);
    }

    void SceneDepth::Initialize(ID3D12Device* device, DescriptorAllocator* descriptorAllocator,
        std::int32_t width, std::int32_t height)
    {
        assert(device != nullptr && "Device must not be null");
        assert(descriptorAllocator != nullptr && "DescriptorAllocator must not be null");

        device_ = device;
        descriptorAllocator_ = descriptorAllocator;
        width_ = width;
        height_ = height;

        CreateDepthStencilResource();

        if (!isInitialized_) {
            // 初回のみDSVを作成
            CreateDepthStencilView();
            isInitialized_ = true;
        } else {
            // 2回目以降は既存のハンドルでDSVを更新
            UpdateDepthStencilView();
        }
    }

    void SceneDepth::ResizeResource(std::int32_t width, std::int32_t height)
    {
        assert(isInitialized_ && "SceneDepth must be initialized first");

        width_ = width;
        height_ = height;

        // リソースを再作成
        CreateDepthStencilResource();

        // 旧リソースの追跡状態を引き継ぐと次のバリアで Before 状態不一致
        // (D3D12 ERROR #527) となりデバッグレイヤーがブレークする。
        // CreateDepthStencilResource() 内の GpuResource::Reset() が
        // 生成と同時に初期ステートを宣言し直すので、ここでの手当ては要らない

        // 既存のハンドルでDSVを更新
        UpdateDepthStencilView();

#ifdef _DEBUG
        logger.Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "SceneDepth: リサイズしました ({}x{}) - DSV/SRV は同じスロットへ書き直し\n", width, height);
#endif
    }

    void SceneDepth::BeginDepthWrite(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(isInitialized_ && "SceneDepth must be initialized before use");

        // DEPTH_WRITE 状態へ遷移（既に DEPTH_WRITE なら冗長バリアをスキップ）
        Barrier::Transition(cmdList, depthStencilResource_, D3D12_RESOURCE_STATE_DEPTH_WRITE);

        // 深度バッファをクリア
        cmdList->ClearDepthStencilView(dsvDescriptor_.cpuHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

#ifdef _DEBUG
        logger.Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
            "[SceneDepth] BeginDepthWrite: クリア完了");
#endif
    }

    void SceneDepth::CreateDepthStencilResource()
    {
        // Typeless フォーマットで作成することで DSV（D24_UNORM_S8）と SRV（R24_UNORM_X8）の両方を作成できる
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Width = width_;
        resourceDesc.Height = height_;
        resourceDesc.MipLevels = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        // クリア値の設定
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

        // リソースを作成
        depthStencilResource_.Reset(
            ResourceFactory::CreateTextureResource(
                device_,
                resourceDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue),
            D3D12_RESOURCE_STATE_DEPTH_WRITE);

#ifdef _DEBUG
        logger.Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "SceneDepth: 深度ステンシルリソースを作成しました ({}x{})\n", width_, height_);
#endif
    }

    void SceneDepth::CreateDepthStencilView()
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        // DSV を作成（初回のみ）。戻り値のハンドルがスロットの所有権を表す
        dsvDescriptor_ = descriptorAllocator_->CreateDSV(
            depthStencilResource_.Get(), dsvDesc, "SceneDepth");

        // 深度リソースの SRV を作成（初回のみ）
        CreateDepthShaderResourceView();
    }

    void SceneDepth::UpdateDepthStencilView()
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        // 既存スロットへビューだけ書き直す（スロット番号は変えない＝シェーダ側のバインドを保つ）
        descriptorAllocator_->WriteDSV(dsvDescriptor_, depthStencilResource_.Get(), dsvDesc);

        if (depthSRVDescriptor_.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels       = 1;
            descriptorAllocator_->WriteSRV(depthSRVDescriptor_, depthStencilResource_.Get(), srvDesc);
        }
    }

    void SceneDepth::CreateDepthShaderResourceView()
    {
        // R24G8_TYPELESS リソースから R24_UNORM_X8_TYPELESS として深度値を読み取る SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;

        depthSRVDescriptor_ = descriptorAllocator_->CreateSRV(
            depthStencilResource_.Get(), srvDesc, "SceneDepthSRV");
    }
}

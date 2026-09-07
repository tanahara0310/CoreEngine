#include "pch.h"
#include "Text3DRenderer.h"

#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Math/MathCore.h"
#include "Text/MsdfFont.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void Text3DRenderer::Initialize(ID3D12Device* device)
    {
        shaderCompiler_->Initialize();

        auto vertexShaderBlob =
            shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Text3D/Text3D.VS.hlsl", L"vs_6_0");
        assert(vertexShaderBlob != nullptr);

        auto pixelShaderBlob =
            shaderCompiler_->CompileShader(L"Engine/Assets/Shaders/Text3D/Text3D.PS.hlsl", L"ps_6_0");
        assert(pixelShaderBlob != nullptr);

        reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
        reflectionData_ = reflectionBuilder_->BuildFromShaders(
            vertexShaderBlob, pixelShaderBlob, "Text3DRenderer");

        RootSignatureConfig config;

        // Linear は必須。ポイントサンプリングにすると距離場の補間が効かず、
        // MSDF がただの低解像度ビットマップに退化する。
        // CLAMP はアトラス端でのラップ回避（グリフが端に接したときに
        // 反対側の距離場を拾うのを防ぐ）。
        SamplerConfig sampler = SamplerConfig::Linear();
        sampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        config.ConfigureSampler("gSampler", sampler);

        auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error(
                "Failed to create Text3D Root Signature: " + buildResult.errorMessage);
        }

        // ── 深度テスト版（既定）────────────────────────────────
        // 深度書き込みを切ってあるのは、MSDF のアンチエイリアス縁が
        // 半端な α でも深度を書いてしまい、後から描かれる背景や
        // パーティクルが文字の周囲の矩形状に抜けるため。
        // 両面描画にしてあるのは、看板を裏から見たときに消えないようにするため。
        const bool result = psoMg_->CreateBuilder()
            .SetDebugName("Text3D")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(true, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob,
                rootSignatureMg_->GetRootSignature());

        if (!result) {
            throw std::runtime_error("Failed to create Text3D Pipeline State Object");
        }

        // ── オーバーレイ版（深度テストも切る）──────────────────
        // ネームプレートやダメージ数値のように、壁越しでも常に見せたいもの向け
        const bool overlayResult = overlayPsoMg_->CreateBuilder()
            .SetDebugName("Text3DOverlay")
            .SetInputLayoutFromReflection(*reflectionData_)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(false, false)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .BuildAllBlendModes(device, vertexShaderBlob, pixelShaderBlob,
                rootSignatureMg_->GetRootSignature());

        if (!overlayResult) {
            throw std::runtime_error("Failed to create Text3D overlay Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);

        CreateSharedIndexBuffer(device);

        batchVertices_.reserve(static_cast<size_t>(kMaxGlyphsPerBatch) * 4);
    }

    void Text3DRenderer::Initialize(GraphicsCore* dxCommon, ResourceFactory* resourceFactory)
    {
        dxCommon_ = dxCommon;
        resourceFactory_ = resourceFactory;

        Initialize(dxCommon->GetDevice());
    }

    void Text3DRenderer::CreateSharedIndexBuffer(ID3D12Device* device)
    {
        constexpr uint32_t kIndexCount = kMaxGlyphsPerBatch * 6;

        indexResource_ = ResourceFactory::CreateBufferResource(
            device, sizeof(uint32_t) * kIndexCount);
        if (!indexResource_) {
            throw std::runtime_error("Failed to create Text3D shared index buffer");
        }

        uint32_t* indices = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indices));
        for (uint32_t glyph = 0; glyph < kMaxGlyphsPerBatch; ++glyph) {
            const uint32_t base = glyph * 4;
            uint32_t* destination = indices + glyph * 6;
            destination[0] = base + 0;
            destination[1] = base + 1;
            destination[2] = base + 2;
            destination[3] = base + 1;
            destination[4] = base + 3;
            destination[5] = base + 2;
        }
        indexResource_->Unmap(0, nullptr);

        indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = sizeof(uint32_t) * kIndexCount;
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    }

    int Text3DRenderer::GetRootParamIndex(const std::string& resourceName) const
    {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }

    ID3D12PipelineState* Text3DRenderer::ResolvePipelineState(
        Text3DDepthMode depthMode, BlendMode blendMode) const
    {
        return (depthMode == Text3DDepthMode::Overlay)
            ? overlayPsoMg_->GetPipelineState(blendMode)
            : psoMg_->GetPipelineState(blendMode);
    }

    void Text3DRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode)
    {
        // ブレンドモードが変わるとここが再度呼ばれる。
        // 積んであるぶんは今の設定で描き切ってから切り替える
        Flush();

        cmdList_ = cmdList;
        currentBlendMode_ = blendMode;

        cmdList->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // PSO は Flush() で決める。深度モードがテキストごとに違うので、
        // ここで固定するとバッチの中身と食い違う
    }

    void Text3DRenderer::EndPass()
    {
        Flush();

        // 1 フレーム（1 パス）の集計を確定させる
        lastFrameDrawCalls_ = frameDrawCalls_;
        lastFrameGlyphs_ = frameGlyphs_;

        frameDrawCalls_ = 0;
        frameGlyphs_ = 0;

        cmdList_ = nullptr;
    }

    void Text3DRenderer::Submit(const MsdfFont* font,
        const TextGlyphVertex* glyphVertices, size_t vertexCount,
        const Matrix4x4& world,
        const Matrix4x4& viewProjection,
        const Text3DDrawStyle& style,
        Text3DDepthMode depthMode)
    {
        if (!font || !glyphVertices || vertexCount == 0) { return; }

        // バッチを切る条件は 3 つ。
        //  - フォント: アトラス（ディスクリプタ）と定数が変わる
        //  - 深度モード: PSO が変わる
        //  - ビュー射影: 定数が変わる（同一パス内では普通は変わらない）
        const bool batchBroken = batchFont_ &&
            (batchFont_ != font
                || batchDepthMode_ != depthMode
                || std::memcmp(&batchViewProjection_, &viewProjection, sizeof(Matrix4x4)) != 0);
        if (batchBroken) {
            Flush();
        }

        batchFont_ = font;
        batchDepthMode_ = depthMode;
        batchViewProjection_ = viewProjection;

        // 共有インデックスバッファの長さを超えるなら、いったん描いてから続ける
        const size_t maxVertices = static_cast<size_t>(kMaxGlyphsPerBatch) * 4;
        if (batchVertices_.size() + vertexCount > maxVertices) {
            Flush();
            batchFont_ = font;
            batchDepthMode_ = depthMode;
            batchViewProjection_ = viewProjection;
        }

        const Vector2 styleParams = { style.outlineWidthEm, style.weightEm };

        batchVertices_.reserve(batchVertices_.size() + vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const TextGlyphVertex& source = glyphVertices[i];

            // em → ワールド。ここで潰しておくことで、テキストごとの
            // 行列を定数バッファへ渡す必要が無くなり、1 本にまとめられる。
            // ワールド行列はアフィンなので w 除算なしの変換でよい
            const Vector4 local = { source.position.x, source.position.y, 0.0f, 1.0f };
            const Vector4 transformed = CoordinateTransform::TransformCoord(local, world);

            batchVertices_.push_back(Text3DVertex{
                transformed,
                source.texcoord,
                style.color,
                style.outlineColor,
                styleParams,
                });
        }
    }

    void Text3DRenderer::Flush()
    {
        if (batchVertices_.empty()) {
            batchFont_ = nullptr;
            return;
        }
        if (!cmdList_ || !batchFont_ || !dxCommon_) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        ID3D12PipelineState* pipelineState = ResolvePipelineState(batchDepthMode_, currentBlendMode_);
        if (!pipelineState) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        // ── バッチ共通の定数（ビュー射影とアトラス情報）────────────
        Text3DBatchConstants constants{};
        constants.viewProjection = batchViewProjection_;
        constants.pxRange = batchFont_->GetPxRange();
        constants.atlasWidth = batchFont_->GetAtlasSize().x;
        constants.atlasHeight = batchFont_->GetAtlasSize().y;
        constants.sdUnitsPerEm = (constants.pxRange > 0.0f)
            ? (static_cast<float>(batchFont_->GetGlyphPixelSize()) / constants.pxRange)
            : 1.0f;

        UploadRing& uploadRing = dxCommon_->GetUploadRing();

        const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress =
            uploadRing.AllocateConstants(constants);
        if (constantsAddress == 0) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        // ── 頂点（フレーム単位で巻き戻る UPLOAD ヒープへ積む）──────
        const uint32_t byteSize =
            static_cast<uint32_t>(sizeof(Text3DVertex) * batchVertices_.size());
        const UploadAllocation allocation = uploadRing.Allocate(byteSize, 16);
        if (!allocation.IsValid()) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }
        std::memcpy(allocation.cpu, batchVertices_.data(), byteSize);

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = allocation.gpuAddress;
        vertexBufferView.SizeInBytes = byteSize;
        vertexBufferView.StrideInBytes = sizeof(Text3DVertex);

        // 深度モードごとに PSO が違うので、バッチを描く直前に張る
        cmdList_->SetPipelineState(pipelineState);
        pipelineState_ = pipelineState;

        cmdList_->SetGraphicsRootConstantBufferView(
            GetRootParamIndex("gBatch"), constantsAddress);
        cmdList_->SetGraphicsRootDescriptorTable(
            GetRootParamIndex("gAtlas"), batchFont_->GetAtlasGpuHandle());

        cmdList_->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdList_->IASetIndexBuffer(&indexBufferView_);

        const uint32_t glyphCount = static_cast<uint32_t>(batchVertices_.size() / 4);
        cmdList_->DrawIndexedInstanced(glyphCount * 6, 1, 0, 0, 0);

        ++frameDrawCalls_;
        frameGlyphs_ += glyphCount;

        batchVertices_.clear();
        batchFont_ = nullptr;
    }
}

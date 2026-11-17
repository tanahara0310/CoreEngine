#include "pch.h"
#include "Text3DRenderer.h"

#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderReflectionData.h"

#include <cstring>
#include <stdexcept>

namespace CoreEngine
{
    void Text3DRenderer::Initialize(ID3D12Device* device)
    {
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

        const auto shaders = BuildShadersAndRootSignature(device,
            L"Engine/Assets/Shaders/Text3D/Text3D.VS.hlsl", L"Engine/Assets/Shaders/Text3D/Text3D.PS.hlsl",
            "Text3DRenderer", config, "Text3D");

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
            .BuildAllBlendModes(device, shaders.vs.Get(), shaders.ps.Get(),
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
            .BuildAllBlendModes(device, shaders.vs.Get(), shaders.ps.Get(),
                rootSignatureMg_->GetRootSignature());

        if (!overlayResult) {
            throw std::runtime_error("Failed to create Text3D overlay Pipeline State Object");
        }

        pipelineState_ = psoMg_->GetPipelineState(BlendMode::kBlendModeNormal);

        batch_.Initialize(device, "Failed to create Text3D shared index buffer");
    }

    void Text3DRenderer::Initialize(GraphicsCore* dxCommon)
    {
        dxCommon_ = dxCommon;

        Initialize(dxCommon->GetDevice());
    }

    int Text3DRenderer::GetRootParamIndex(const std::string& resourceName) const
    {
        return FindRootParamIndex(reflectionData_.get(), resourceName);
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
        batch_.EndFrame();

        cmdList_ = nullptr;
    }

    void Text3DRenderer::Submit(const MsdfFont* font,
        const TextGlyphVertex* glyphVertices, size_t vertexCount,
        const Matrix4x4& world,
        const Matrix4x4& viewProjection,
        const TextDrawStyle& style,
        Text3DDepthMode depthMode)
    {
        if (!font || !glyphVertices || vertexCount == 0) { return; }

        // バッチを切る条件は 3 つ。
        //  - フォント: アトラス（ディスクリプタ）と定数が変わる
        //  - 深度モード: PSO が変わる
        //  - ビュー射影: 定数が変わる（同一パス内では普通は変わらない）
        const bool batchBroken = batch_.GetFont() &&
            (batch_.GetFont() != font
                || batchDepthMode_ != depthMode
                || std::memcmp(&batchViewProjection_, &viewProjection, sizeof(Matrix4x4)) != 0);
        if (batchBroken) {
            Flush();
        }

        batchDepthMode_ = depthMode;
        batchViewProjection_ = viewProjection;

        // 共有インデックスバッファの長さを超えるなら、いったん描いてから続ける
        if (batch_.WouldOverflow(vertexCount)) {
            Flush();
        }

        // ワールド行列はアフィンなので w 除算なしの変換でよい
        batch_.Append(font, glyphVertices, vertexCount, world, style);
    }

    void Text3DRenderer::Flush()
    {
        ID3D12PipelineState* pipelineState = nullptr;
        if (!batch_.IsEmpty() && cmdList_ && batch_.GetFont() && dxCommon_) {
            pipelineState = ResolvePipelineState(batchDepthMode_, currentBlendMode_);
        }
        if (!pipelineState) {
            batch_.Clear();
            return;
        }

        // 深度モードごとに PSO が違うので、バッチを描く直前に張る
        if (batch_.Draw(cmdList_, dxCommon_->GetUploadRing(), batchViewProjection_,
                GetRootParamIndex("gBatch"), GetRootParamIndex("gAtlas"), pipelineState)) {
            pipelineState_ = pipelineState;
        }
    }
}

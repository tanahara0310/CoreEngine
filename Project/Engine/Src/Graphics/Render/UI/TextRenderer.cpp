#include "pch.h"
#include "TextRenderer.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Text/MsdfFont.h"
#include "Utility/Logger/Logger.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void TextRenderer::Initialize(ID3D12Device* device)
    {
        // PSO / ルートシグネチャは UIRenderer の実装をそのまま使う
        // （差し替えるのは GetVertexShaderPath 等のフックだけ）
        UIRenderer::Initialize(device);

        CreateSharedIndexBuffer(device);

        batchVertices_.reserve(static_cast<size_t>(kMaxGlyphsPerBatch) * 4);
    }

    void TextRenderer::CreateSharedIndexBuffer(ID3D12Device* device)
    {
        // グリフ 1 つ = 四角形 1 枚。1 バッチの最大グリフ数ぶんを並べる
        indexResource_ = ResourceFactory::CreateQuadIndexBuffer(device, indexBufferView_, kMaxGlyphsPerBatch);
        if (!indexResource_) {
            throw std::runtime_error("Failed to create MsdfText shared index buffer");
        }
    }

    void TextRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode)
    {
        // ブレンドモードが変わるとここが再度呼ばれて PSO が差し替わる。
        // 積んであるぶんは今の設定で描き切ってから切り替える
        Flush();

        UIRenderer::BeginPass(cmdList, blendMode);
        cmdList_ = cmdList;
    }

    void TextRenderer::EndPass()
    {
        Flush();

        // 1 フレーム（1 パス）の集計を確定させる
        lastFrameDrawCalls_ = frameDrawCalls_;
        lastFrameGlyphs_ = frameGlyphs_;

        // バッチの効き具合が変わったときだけ残す（毎フレームは出さない）
        if (frameDrawCalls_ != loggedDrawCalls_ && frameDrawCalls_ > 0) {
            loggedDrawCalls_ = frameDrawCalls_;
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "MsdfText: テキスト描画 {} ドローコール / {} グリフ（バッチング後）",
                frameDrawCalls_, frameGlyphs_);
        }

        frameDrawCalls_ = 0;
        frameGlyphs_ = 0;

        cmdList_ = nullptr;

        UIRenderer::EndPass();
    }

    void TextRenderer::Submit(const MsdfFont* font,
        const TextGlyphVertex* glyphVertices, size_t vertexCount,
        const Matrix4x4& world,
        const TextDrawStyle& style)
    {
        if (!font || !glyphVertices || vertexCount == 0) { return; }

        // フォントが変わるとアトラス（ディスクリプタ）も定数も変わるので、
        // ここでバッチを切る。UI は 1 フォントで作ることが多いので実質 1 本にまとまる
        if (batchFont_ && batchFont_ != font) {
            Flush();
        }
        batchFont_ = font;

        // 共有インデックスバッファの長さを超えるなら、いったん描いてから続ける
        const size_t maxVertices = static_cast<size_t>(kMaxGlyphsPerBatch) * 4;
        if (batchVertices_.size() + vertexCount > maxVertices) {
            Flush();
            batchFont_ = font;
        }

        const Vector2 styleParams = { style.outlineWidthEm, style.weightEm };

        batchVertices_.reserve(batchVertices_.size() + vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const TextGlyphVertex& source = glyphVertices[i];

            // em → スクリーン px。ここで潰しておくことで、テキストごとの
            // 行列を定数バッファへ渡す必要が無くなり、1 本にまとめられる
            const Vector4 local = { source.position.x, source.position.y, 0.0f, 1.0f };
            const Vector4 transformed = CoordinateTransform::TransformCoord(local, world);

            batchVertices_.push_back(TextVertex{
                transformed,
                source.texcoord,
                style.color,
                style.outlineColor,
                styleParams,
                });
        }
    }

    void TextRenderer::Flush()
    {
        if (batchVertices_.empty()) {
            batchFont_ = nullptr;
            return;
        }
        GraphicsCore* graphicsCore = GetGraphicsCore();
        if (!cmdList_ || !batchFont_ || !graphicsCore) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        // ── バッチ共通の定数（射影とアトラス情報）────────────────
        TextBatchConstants constants{};
        constants.projection = CalculateProjectionMatrix();
        constants.pxRange = batchFont_->GetPxRange();
        constants.atlasWidth = batchFont_->GetAtlasSize().x;
        constants.atlasHeight = batchFont_->GetAtlasSize().y;
        constants.sdUnitsPerEm = (constants.pxRange > 0.0f)
            ? (static_cast<float>(batchFont_->GetGlyphPixelSize()) / constants.pxRange)
            : 1.0f;

        UploadRing& uploadRing = graphicsCore->GetUploadRing();

        const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress =
            uploadRing.AllocateConstants(constants);
        if (constantsAddress == 0) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        // ── 頂点（フレーム単位で巻き戻る UPLOAD ヒープへ積む）──────
        const UploadAllocation allocation = uploadRing.AllocateArray<TextVertex>(batchVertices_);
        if (!allocation.IsValid()) {
            batchVertices_.clear();
            batchFont_ = nullptr;
            return;
        }

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = allocation.gpuAddress;
        vertexBufferView.SizeInBytes = allocation.size;
        vertexBufferView.StrideInBytes = sizeof(TextVertex);

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

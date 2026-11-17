#include "pch.h"
#include "MsdfTextBatch.h"

#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Math/MathCore.h"
#include "Text/MsdfFont.h"

#include <stdexcept>

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void MsdfTextBatch::Initialize(ID3D12Device* device, const char* errorMessage)
    {
        // グリフ 1 つ = 四角形 1 枚。1 バッチの最大グリフ数ぶんを並べる
        indexResource_ = ResourceFactory::CreateQuadIndexBuffer(device, indexBufferView_, kMaxGlyphsPerBatch);
        if (!indexResource_) {
            throw std::runtime_error(errorMessage);
        }

        vertices_.reserve(static_cast<size_t>(kMaxGlyphsPerBatch) * 4);
    }

    bool MsdfTextBatch::WouldOverflow(size_t vertexCount) const
    {
        const size_t maxVertices = static_cast<size_t>(kMaxGlyphsPerBatch) * 4;
        return vertices_.size() + vertexCount > maxVertices;
    }

    void MsdfTextBatch::Append(const MsdfFont* font,
        const TextGlyphVertex* glyphVertices, size_t vertexCount,
        const Matrix4x4& world,
        const TextDrawStyle& style)
    {
        font_ = font;

        const Vector2 styleParams = { style.outlineWidthEm, style.weightEm };

        vertices_.reserve(vertices_.size() + vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            const TextGlyphVertex& source = glyphVertices[i];

            // em → スクリーン px またはワールド。ここで潰しておくことで、テキストごとの
            // 行列を定数バッファへ渡す必要が無くなり、1 本にまとめられる
            const Vector4 local = { source.position.x, source.position.y, 0.0f, 1.0f };
            const Vector4 transformed = CoordinateTransform::TransformCoord(local, world);

            vertices_.push_back(TextVertex{
                transformed,
                source.texcoord,
                style.color,
                style.outlineColor,
                styleParams,
                });
        }
    }

    void MsdfTextBatch::Clear()
    {
        vertices_.clear();
        font_ = nullptr;
    }

    bool MsdfTextBatch::Draw(ID3D12GraphicsCommandList* cmdList, UploadRing& uploadRing,
        const Matrix4x4& projection, RootSlot batchSlot, RootSlot atlasSlot,
        ID3D12PipelineState* pipelineState)
    {
        // ── バッチ共通の定数（射影とアトラス情報）────────────────
        TextBatchConstants constants{};
        constants.projection = projection;
        constants.pxRange = font_->GetPxRange();
        constants.atlasWidth = font_->GetAtlasSize().x;
        constants.atlasHeight = font_->GetAtlasSize().y;
        constants.sdUnitsPerEm = (constants.pxRange > 0.0f)
            ? (static_cast<float>(font_->GetGlyphPixelSize()) / constants.pxRange)
            : 1.0f;

        const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress =
            uploadRing.AllocateConstants(constants);
        if (constantsAddress == 0) {
            Clear();
            return false;
        }

        // ── 頂点（フレーム単位で巻き戻る UPLOAD ヒープへ積む）──────
        const UploadAllocation allocation = uploadRing.AllocateArray<TextVertex>(vertices_);
        if (!allocation.IsValid()) {
            Clear();
            return false;
        }

        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = allocation.gpuAddress;
        vertexBufferView.SizeInBytes = allocation.size;
        vertexBufferView.StrideInBytes = sizeof(TextVertex);

        if (pipelineState) {
            cmdList->SetPipelineState(pipelineState);
        }

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
        binder.Set(batchSlot, constantsAddress);
        binder.Set(atlasSlot, font_->GetAtlasGpuHandle());

        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdList->IASetIndexBuffer(&indexBufferView_);

        const uint32_t glyphCount = static_cast<uint32_t>(vertices_.size() / 4);
        cmdList->DrawIndexedInstanced(glyphCount * 6, 1, 0, 0, 0);

        ++frameDrawCalls_;
        frameGlyphs_ += glyphCount;

        Clear();
        return true;
    }

    void MsdfTextBatch::EndFrame()
    {
        lastFrameDrawCalls_ = frameDrawCalls_;
        lastFrameGlyphs_ = frameGlyphs_;
        frameDrawCalls_ = 0;
        frameGlyphs_ = 0;
    }
}

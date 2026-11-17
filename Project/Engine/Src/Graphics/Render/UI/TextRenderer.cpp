#include "pch.h"
#include "TextRenderer.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/Logger/Logger.h"

#include <cstdint>

namespace CoreEngine
{
    void TextRenderer::Initialize(ID3D12Device* device)
    {
        // PSO / ルートシグネチャは UIRenderer の実装をそのまま使う
        // （差し替えるのは GetVertexShaderPath 等のフックだけ）
        UIRenderer::Initialize(device);

        batch_.Initialize(device, "Failed to create MsdfText shared index buffer");
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
        batch_.EndFrame();

        // バッチの効き具合が変わったときだけ残す（毎フレームは出さない）
        const uint32_t drawCalls = batch_.GetLastFrameDrawCallCount();
        if (drawCalls != loggedDrawCalls_ && drawCalls > 0) {
            loggedDrawCalls_ = drawCalls;
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "MsdfText: テキスト描画 {} ドローコール / {} グリフ（バッチング後）",
                drawCalls, batch_.GetLastFrameGlyphCount());
        }

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
        if (batch_.GetFont() && batch_.GetFont() != font) {
            Flush();
        }

        // 共有インデックスバッファの長さを超えるなら、いったん描いてから続ける
        if (batch_.WouldOverflow(vertexCount)) {
            Flush();
        }

        batch_.Append(font, glyphVertices, vertexCount, world, style);
    }

    void TextRenderer::Flush()
    {
        GraphicsCore* graphicsCore = GetGraphicsCore();
        if (batch_.IsEmpty() || !cmdList_ || !batch_.GetFont() || !graphicsCore) {
            batch_.Clear();
            return;
        }

        batch_.Draw(cmdList_, graphicsCore->GetUploadRing(), CalculateProjectionMatrix(),
            GetRootParamIndex("gBatch"), GetRootParamIndex("gAtlas"));
    }
}

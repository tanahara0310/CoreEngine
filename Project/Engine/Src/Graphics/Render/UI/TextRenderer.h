#pragma once

#include "Graphics/Render/UI/UIRenderer.h"
#include "Text/MsdfTextBatch.h"

#include <cstdint>

namespace CoreEngine
{
    class MsdfFont;

    /// @brief MSDF テキスト描画専用レンダラー
    /// @details
    ///  スクリーン固定の正射影は UI と全く同じなので UIRenderer を土台にし、
    ///  シェーダーとサンプラーだけを差し替える。
    ///
    ///  UI と別パスに分けているのは、RenderManager が
    ///  「パス種別が変わったときだけ BeginPass（＝PSO 切り替え）」で束ねるため。
    ///  同じ UI パスに相乗りさせると、UI テキストの後に描かれる UI 画像が
    ///  MSDF 用 PSO のまま描かれてしまう。
    ///
    ///  **バッチング**:
    ///  `UITextComponent::Render` はドローコールを発行せず Submit() で頂点を積むだけ。
    ///  実際の描画は Flush()（BeginPass / EndPass / フォント切り替え / 容量超過）で
    ///  1 回にまとめて行う。テキストごとの色・縁取り・行列は頂点へ焼き込んであるので、
    ///  何個並べてもドローコールは（フォントの種類数だけ）で済む。
    class TextRenderer : public UIRenderer
    {
    public:
        /// @brief 1 バッチに積めるグリフ数の上限
        /// @note 共有インデックスバッファの長さがそのまま上限になる。
        ///       超えたらその場でフラッシュして次のバッチへ続ける（描画は欠けない）
        static constexpr uint32_t kMaxGlyphsPerBatch = MsdfTextBatch::kMaxGlyphsPerBatch;

        /// @brief 1 つの UI テキストが描けるグリフ数の上限
        static constexpr uint32_t kMaxGlyphsPerText = kMaxGlyphsPerBatch;

        RenderPassType GetRenderPassType() const override { return RenderPassType::UIText; }

        /// @brief 基底の Initialize(GraphicsCore*) を隠さない
        /// @note 下で Initialize(ID3D12Device*) を宣言すると、同名の基底オーバーロードが
        ///       名前隠蔽で見えなくなる（呼び出し側は 2 引数版を使う）
        using UIRenderer::Initialize;

        /// @brief PSO の構築に加えて、全 UI テキストで共有するインデックスバッファを作る
        void Initialize(ID3D12Device* device) override;

        /// @brief 溜まっているバッチを描いてから、ルートシグネチャと PSO を張り直す
        /// @note ブレンドモードが変わると RenderManager がここを再度呼ぶ。
        ///       PSO が変わる前に描いておかないと、積んだぶんが違う設定で出てしまう
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;

        /// @brief 溜まっているバッチを描き切る
        void EndPass() override;

        /// @brief UI テキスト 1 件ぶんの頂点を積む（ドローコールはここでは出ない）
        /// @param font 使用フォント。変わったらその時点でフラッシュする
        /// @param glyphVertices em 単位の頂点（4 頂点 = 1 グリフ）
        /// @param vertexCount 頂点数（4 の倍数）
        /// @param world em → スクリーン px の変換
        /// @param style 色・縁取り・太さ
        void Submit(const MsdfFont* font,
            const TextGlyphVertex* glyphVertices, size_t vertexCount,
            const Matrix4x4& world,
            const TextDrawStyle& style);

        // ===== デバッグ表示用 =====
        /// @brief 直近フレームのドローコール数（バッチングの効きを見る）
        uint32_t GetLastFrameDrawCallCount() const { return batch_.GetLastFrameDrawCallCount(); }
        /// @brief 直近フレームに描いたグリフ数
        uint32_t GetLastFrameGlyphCount() const { return batch_.GetLastFrameGlyphCount(); }

    protected:
        const wchar_t* GetVertexShaderPath() const override
        {
            return L"Engine/Assets/Shaders/UI/MsdfText.VS.hlsl";
        }

        const wchar_t* GetPixelShaderPath() const override
        {
            return L"Engine/Assets/Shaders/UI/MsdfText.PS.hlsl";
        }

        const char* GetPipelineDebugName() const override { return "MsdfText"; }

        /// @brief テキストは定数バッファプールを使わない
        /// @details 色も行列も頂点へ焼き込むので、UIRenderer のプール（テキスト分で
        ///          約 1.5MB）は丸ごと不要。0 を返して確保させない
        size_t GetConstantBufferPoolCount() const override { return 0; }

        /// @brief リニア補間 + CLAMP
        /// @details
        ///  - Linear は必須。ポイントサンプリングにすると距離場の補間が効かず、
        ///    MSDF がただの低解像度ビットマップに退化する。
        ///  - CLAMP はアトラス端でのラップ回避。パディングがあるので実害は稀だが、
        ///    グリフが端に接したときに反対側の距離場を拾うのを防ぐ。
        SamplerConfig GetSamplerConfig() const override
        {
            SamplerConfig config = SamplerConfig::Linear();
            config.addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            config.addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            return config;
        }

    private:
        /// @brief 溜まっている頂点を 1 ドローコールで描く
        void Flush();

        // ── バッチ状態 ────────────────────────────────────────────
        /// 記録先。BeginPass で受け取り、EndPass のフラッシュでも使う
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        /// 積んでいる頂点（フォントが変わったらフラッシュ）
        MsdfTextBatch batch_;
        /// 直近にログへ出したドローコール数（変化したときだけ出す）
        uint32_t loggedDrawCalls_ = 0;
    };
}

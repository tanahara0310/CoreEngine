#pragma once

#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <cstdint>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;

    /// @brief UIText が持っている変換前のグリフ頂点
    /// @details
    ///  位置は **em 単位**（フォントサイズ 1.0 のときの大きさ）。
    ///  フォントサイズ・位置・回転はレンダラーへ積むときに掛ける。
    ///  こうしておくと、サイズを変えても UIText 側の頂点は組み直さずに済む。
    struct TextGlyphVertex
    {
        Vector2 position;
        /// xy = アトラス UV / z = アトラス配列の何枚目か
        Vector3 texcoord;
    };

    /// @brief 1 つの UIText の見た目（頂点へ焼き込んでバッチにまとめる）
    /// @details
    ///  以前はテキストごとの定数バッファに入れていたが、それだと
    ///  テキストの数だけドローコールが要る。頂点へ載せると 1 本にまとめられる。
    struct TextDrawStyle
    {
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        float outlineWidthEm = 0.0f;
        float weightEm = 0.0f;
    };

    /// @brief GPU へ送るテキスト頂点（MsdfText.VS.hlsl の入力と一致させること）
    /// @note 位置は **スクリーン px**。em → px の変換は CPU 側で済ませてある
    ///       （テキストごとの行列を無くさないとバッチにまとめられないため）。
    struct TextVertex
    {
        Vector4 position;     ///< POSITION0
        Vector3 texcoord;     ///< TEXCOORD0（xy = UV / z = 枚番号）
        Vector4 color;        ///< COLOR0
        Vector4 outlineColor; ///< COLOR1
        Vector2 style;        ///< TEXCOORD1（x = 縁取り幅 em / y = 太さ em）
    };

    static constexpr Cb::Field kTextVertexFields[] = {
        CB_FIELD(TextVertex, position), CB_FIELD(TextVertex, texcoord),
        CB_FIELD(TextVertex, color), CB_FIELD(TextVertex, outlineColor),
        CB_FIELD(TextVertex, style),
    };
    CB_VERIFY_STRIDE(TextVertex, kTextVertexFields);

    /// @brief バッチ共通の定数（HLSL 側 `ConstantBuffer<TextBatch> gBatch` と一致させること）
    /// @details
    ///  ここに入るのは「1 バッチのあいだ変わらないもの」だけ。
    ///  射影はパス全体で共通、アトラス情報はフォント単位なので、
    ///  フォントが変わるところでバッチを切れば定数はこれで足りる。
    struct TextBatchConstants
    {
        Matrix4x4 projection; ///< スクリーン px → クリップ空間

        float pxRange;      ///< 距離場の有効範囲（px）
        float atlasWidth;   ///< アトラス 1 枚あたりの画素サイズ
        float atlasHeight;
        float sdUnitsPerEm; ///< em → 距離場の値（= glyphPixelSize / pxRange）
    };

    static constexpr Cb::Field kTextBatchConstantsFields[] = {
        CB_FIELD(TextBatchConstants, projection),
        CB_FIELD(TextBatchConstants, pxRange),
        CB_FIELD(TextBatchConstants, atlasWidth),
        CB_FIELD(TextBatchConstants, atlasHeight),
        CB_FIELD(TextBatchConstants, sdUnitsPerEm),
    };
    CB_VERIFY_LAYOUT(TextBatchConstants, kTextBatchConstantsFields);

    /// @brief MSDF テキスト描画専用レンダラー
    /// @details
    ///  スクリーン固定の正射影は UI と全く同じなので UIRenderer を土台にし、
    ///  シェーダーとサンプラーだけを差し替える。
    ///
    ///  UI と別パスに分けているのは、RenderManager が
    ///  「パス種別が変わったときだけ BeginPass（＝PSO 切り替え）」で束ねるため。
    ///  同じ UI パスに相乗りさせると、UIText の後に描かれる UIImage が
    ///  MSDF 用 PSO のまま描かれてしまう。
    ///
    ///  **バッチング**:
    ///  `UIText::Draw` はドローコールを発行せず Submit() で頂点を積むだけ。
    ///  実際の描画は Flush()（BeginPass / EndPass / フォント切り替え / 容量超過）で
    ///  1 回にまとめて行う。テキストごとの色・縁取り・行列は頂点へ焼き込んであるので、
    ///  何個並べてもドローコールは（フォントの種類数だけ）で済む。
    class TextRenderer : public UIRenderer
    {
    public:
        /// @brief 1 バッチに積めるグリフ数の上限
        /// @note 共有インデックスバッファの長さがそのまま上限になる。
        ///       超えたらその場でフラッシュして次のバッチへ続ける（描画は欠けない）
        static constexpr uint32_t kMaxGlyphsPerBatch = 8192;

        /// @brief 1 つの UIText が描けるグリフ数の上限
        static constexpr uint32_t kMaxGlyphsPerText = kMaxGlyphsPerBatch;

        RenderPassType GetRenderPassType() const override { return RenderPassType::UIText; }

        /// @brief 基底の Initialize(GraphicsCore*, ResourceFactory*) を隠さない
        /// @note 下で Initialize(ID3D12Device*) を宣言すると、同名の基底オーバーロードが
        ///       名前隠蔽で見えなくなる（呼び出し側は 2 引数版を使う）
        using UIRenderer::Initialize;

        /// @brief PSO の構築に加えて、全 UIText で共有するインデックスバッファを作る
        void Initialize(ID3D12Device* device) override;

        /// @brief 溜まっているバッチを描いてから、ルートシグネチャと PSO を張り直す
        /// @note ブレンドモードが変わると RenderManager がここを再度呼ぶ。
        ///       PSO が変わる前に描いておかないと、積んだぶんが違う設定で出てしまう
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;

        /// @brief 溜まっているバッチを描き切る
        void EndPass() override;

        /// @brief UIText 1 件ぶんの頂点を積む（ドローコールはここでは出ない）
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
        uint32_t GetLastFrameDrawCallCount() const { return lastFrameDrawCalls_; }
        /// @brief 直近フレームに描いたグリフ数
        uint32_t GetLastFrameGlyphCount() const { return lastFrameGlyphs_; }

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
        /// @brief 共有インデックスバッファを生成して内容を書き込む
        void CreateSharedIndexBuffer(ID3D12Device* device);

        /// @brief 溜まっている頂点を 1 ドローコールで描く
        void Flush();

        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        // ── バッチ状態 ────────────────────────────────────────────
        /// 記録先。BeginPass で受け取り、EndPass のフラッシュでも使う
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        /// いま積んでいるバッチのフォント（変わったらフラッシュ）
        const MsdfFont* batchFont_ = nullptr;
        std::vector<TextVertex> batchVertices_;

        uint32_t frameDrawCalls_ = 0;
        uint32_t frameGlyphs_ = 0;
        uint32_t lastFrameDrawCalls_ = 0;
        uint32_t lastFrameGlyphs_ = 0;
        /// 直近にログへ出したドローコール数（変化したときだけ出す）
        uint32_t loggedDrawCalls_ = 0;
    };
}

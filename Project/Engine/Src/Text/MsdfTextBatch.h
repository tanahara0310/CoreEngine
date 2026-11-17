#pragma once

#include "Graphics/RootSignature/RootSlot.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Text/TextGeometryBuilder.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;
    class UploadRing;

    /// @brief 1 つのテキストの見た目（頂点へ焼き込んでバッチにまとめる）
    /// @details UI テキストと 3D テキストで共通
    struct TextDrawStyle
    {
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        float outlineWidthEm = 0.0f;
        float weightEm = 0.0f;
    };

    /// @brief GPU へ送るテキスト頂点（MsdfText.VS.hlsl と Text3D.VS.hlsl の入力と一致させること）
    /// @note 位置は UI ならスクリーン px、3D ならワールド座標。em からの変換は CPU 側で済ませてある
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

    /// @brief バッチ共通の定数（HLSL 側 `ConstantBuffer<TextBatch> gBatch` / `ConstantBuffer<Text3DBatch> gBatch` と一致させること）
    /// @details 1 バッチのあいだ変わらない射影とアトラス情報だけを入れる
    struct TextBatchConstants
    {
        Matrix4x4 projection; ///< UI はスクリーン px → クリップ空間、3D はワールド → クリップ空間

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

    /// @brief MSDF テキストの頂点を積み、1 フォントぶんを 1 ドローコールで描くバッチ
    /// @details UI テキストと 3D テキストのレンダラーが 1 つずつ持つ。
    ///          バッチを切る条件と PSO・射影の決め方はレンダラー側が持つ
    class MsdfTextBatch
    {
    public:
        /// @brief 1 バッチに積めるグリフ数の上限（共有インデックスバッファの長さ）
        static constexpr uint32_t kMaxGlyphsPerBatch = 8192;

        /// @brief 全テキストで共有するインデックスバッファを作り、頂点の置き場を確保する
        /// @param errorMessage インデックスバッファを作れなかったときに投げる例外の文言
        void Initialize(ID3D12Device* device, const char* errorMessage);

        /// @brief 積んでいる頂点が無いか
        bool IsEmpty() const { return vertices_.empty(); }

        /// @brief 積んでいるバッチのフォント（無ければ nullptr）
        const MsdfFont* GetFont() const { return font_; }

        /// @brief vertexCount 個を足すと共有インデックスバッファの長さを超えるか
        bool WouldOverflow(size_t vertexCount) const;

        /// @brief バッチのフォントを決め、em 単位の頂点を world で変換して積む
        /// @param glyphVertices em 単位の頂点（4 頂点 = 1 グリフ）
        /// @param vertexCount 頂点数（4 の倍数）
        void Append(const MsdfFont* font,
            const TextGlyphVertex* glyphVertices, size_t vertexCount,
            const Matrix4x4& world,
            const TextDrawStyle& style);

        /// @brief 積んだ頂点とフォントを捨てる
        void Clear();

        /// @brief 積んだ頂点を 1 ドローコールで描き、バッチを空にする
        /// @param projection 定数へ入れる射影
        /// @param batchSlot gBatch のルートパラメータ
        /// @param atlasSlot gAtlas のルートパラメータ
        /// @param pipelineState 描く前に張る PSO（nullptr なら張らない）
        /// @return 描いたら true。UploadRing から確保できなければ描かずに false
        /// @note 頂点があり、フォントが決まっているときに呼ぶ
        bool Draw(ID3D12GraphicsCommandList* cmdList, UploadRing& uploadRing,
            const Matrix4x4& projection, RootSlot batchSlot, RootSlot atlasSlot,
            ID3D12PipelineState* pipelineState = nullptr);

        /// @brief 1 フレーム（1 パス）の集計を確定させ、次のフレームへ向けて 0 に戻す
        void EndFrame();

        /// @brief 直近フレームのドローコール数
        uint32_t GetLastFrameDrawCallCount() const { return lastFrameDrawCalls_; }
        /// @brief 直近フレームに描いたグリフ数
        uint32_t GetLastFrameGlyphCount() const { return lastFrameGlyphs_; }

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        /// いま積んでいるバッチのフォント
        const MsdfFont* font_ = nullptr;
        std::vector<TextVertex> vertices_;

        uint32_t frameDrawCalls_ = 0;
        uint32_t frameGlyphs_ = 0;
        uint32_t lastFrameDrawCalls_ = 0;
        uint32_t lastFrameGlyphs_ = 0;
    };
}

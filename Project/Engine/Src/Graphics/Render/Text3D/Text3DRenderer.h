#pragma once

#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Text/TextGeometryBuilder.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;

    /// @brief 3D テキストの深度の扱い
    /// @details
    ///  同じ文字列でも「壁の裏に回れば隠れてほしい看板」と
    ///  「壁越しでも読めてほしいダメージ数値」で要求が正反対になる。
    ///  PSO を 2 本持って、テキストごとに選ばせる。
    enum class Text3DDepthMode : uint8_t
    {
        /// @brief 深度テスト ON・書き込み OFF（既定）
        /// @details
        ///  書き込みを切ってあるのは、MSDF のアンチエイリアス縁が
        ///  α = 0.3 のような半端な画素でも容赦なく深度を書いてしまうため。
        ///  書かせると、後から描かれる背景やパーティクルが
        ///  文字の周囲の矩形状に抜ける。
        ///  失うのはテキスト同士の前後関係だが、RenderManager は
        ///  カメラ距離でソートしないので書き込みを付けても解決しない
        ///  （どちらにせよ登録順で決まる）。必要なら SetRenderOrder で指定する。
        Test,

        /// @brief 深度テスト OFF・書き込み OFF
        /// @details ネームプレートやダメージ数値のように、壁越しでも常に見せたいもの向け
        Overlay,
    };

    /// @brief 1 つの 3D テキストの見た目（頂点へ焼き込んでバッチにまとめる）
    struct Text3DDrawStyle
    {
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        float outlineWidthEm = 0.0f;
        float weightEm = 0.0f;
    };

    /// @brief GPU へ送るテキスト頂点（Text3D.VS.hlsl の入力と一致させること）
    /// @note 位置は **ワールド座標**。em → ワールドの変換は CPU 側で済ませてある
    ///       （テキストごとの行列を無くさないとバッチにまとめられないため）。
    struct Text3DVertex
    {
        Vector4 position;     ///< POSITION0
        Vector3 texcoord;     ///< TEXCOORD0（xy = UV / z = 枚番号）
        Vector4 color;        ///< COLOR0
        Vector4 outlineColor; ///< COLOR1
        Vector2 style;        ///< TEXCOORD1（x = 縁取り幅 em / y = 太さ em）
    };

    static constexpr Cb::Field kText3DVertexFields[] = {
        CB_FIELD(Text3DVertex, position), CB_FIELD(Text3DVertex, texcoord),
        CB_FIELD(Text3DVertex, color), CB_FIELD(Text3DVertex, outlineColor),
        CB_FIELD(Text3DVertex, style),
    };
    CB_VERIFY_STRIDE(Text3DVertex, kText3DVertexFields);

    /// @brief バッチ共通の定数（HLSL 側 `ConstantBuffer<Text3DBatch> gBatch` と一致させること）
    struct Text3DBatchConstants
    {
        Matrix4x4 viewProjection; ///< ワールド → クリップ空間

        float pxRange;      ///< 距離場の有効範囲（px）
        float atlasWidth;   ///< アトラス 1 枚あたりの画素サイズ
        float atlasHeight;
        float sdUnitsPerEm; ///< em → 距離場の値（= glyphPixelSize / pxRange）
    };

    static constexpr Cb::Field kText3DBatchConstantsFields[] = {
        CB_FIELD(Text3DBatchConstants, viewProjection),
        CB_FIELD(Text3DBatchConstants, pxRange),
        CB_FIELD(Text3DBatchConstants, atlasWidth),
        CB_FIELD(Text3DBatchConstants, atlasHeight),
        CB_FIELD(Text3DBatchConstants, sdUnitsPerEm),
    };
    CB_VERIFY_LAYOUT(Text3DBatchConstants, kText3DBatchConstantsFields);

    /// @brief MSDF テキストをワールド空間へ描くレンダラー
    /// @details
    ///  UI 版（TextRenderer）との違いは、頂点へ焼き込む行列がスクリーン px ではなく
    ///  ワールド行列であることと、定数バッファへ渡す射影がカメラのビュー射影であること。
    ///  距離場そのものと、アンチエイリアスの効かせ方（fwidth からの画面 px 換算）は
    ///  そのまま流用できるので、ピクセルシェーダーは UI 版とほぼ同一。
    ///
    ///  **バッチング**:
    ///  `Text3DObject::Draw` はドローコールを発行せず Submit() で頂点を積むだけ。
    ///  実際の描画は Flush()（BeginPass / EndPass / フォント・深度モード・ビューの
    ///  切り替え / 容量超過）で 1 回にまとめて行う。
    ///  テキストごとの色・縁取り・ワールド行列は頂点へ焼き込んであるので、
    ///  何個並べてもドローコールはバッチ数ぶんで済む。
    class Text3DRenderer : public BaseRenderer
    {
    public:
        /// @brief 1 バッチに積めるグリフ数の上限
        /// @note 共有インデックスバッファの長さがそのまま上限になる。
        ///       超えたらその場でフラッシュして次のバッチへ続ける（描画は欠けない）
        static constexpr uint32_t kMaxGlyphsPerBatch = 8192;

        /// @brief 1 つのテキストが描けるグリフ数の上限
        static constexpr uint32_t kMaxGlyphsPerText = kMaxGlyphsPerBatch;

        // ===== IRenderer インターフェース =====
        RenderPassType GetRenderPassType() const override { return RenderPassType::Text3D; }

        /// @brief PSO / ルートシグネチャと共有インデックスバッファを作る
        void Initialize(ID3D12Device* device) override;

        /// @brief 初期化（GraphicsCore 付き。こちらを使うこと）
        void Initialize(GraphicsCore* dxCommon, ResourceFactory* resourceFactory);

        /// @brief 溜まっているバッチを描いてから、ルートシグネチャを張り直す
        /// @note ブレンドモードが変わると RenderManager がここを再度呼ぶ。
        ///       PSO が変わる前に描いておかないと、積んだぶんが違う設定で出てしまう
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;

        /// @brief 溜まっているバッチを描き切る
        void EndPass() override;

        /// @brief カメラは使わない
        /// @details ビュー射影は Submit() で受け取る。パスに紐づくビューは
        ///          DrawViewInfo が持っており、描画側は必ずそこから取る決まりのため
        ///          （レンダラーが別途カメラを読み直すとパスごとに行列が食い違う）。
        void SetCamera(const Camera* camera) override { (void)camera; }

        /// @brief テキスト 1 件ぶんの頂点を積む（ドローコールはここでは出ない）
        /// @param font 使用フォント。変わったらその時点でフラッシュする
        /// @param glyphVertices em 単位の頂点（4 頂点 = 1 グリフ）
        /// @param vertexCount 頂点数（4 の倍数）
        /// @param world em → ワールドの変換
        /// @param viewProjection このテキストを描くビューのビュー射影
        /// @param style 色・縁取り・太さ
        /// @param depthMode 深度の扱い
        void Submit(const MsdfFont* font,
            const TextGlyphVertex* glyphVertices, size_t vertexCount,
            const Matrix4x4& world,
            const Matrix4x4& viewProjection,
            const Text3DDrawStyle& style,
            Text3DDepthMode depthMode);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief GraphicsCore を取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

        // ===== デバッグ表示用 =====
        /// @brief 直近フレームのドローコール数（バッチングの効きを見る）
        uint32_t GetLastFrameDrawCallCount() const { return lastFrameDrawCalls_; }
        /// @brief 直近フレームに描いたグリフ数
        uint32_t GetLastFrameGlyphCount() const { return lastFrameGlyphs_; }

    private:
        /// @brief 共有インデックスバッファを生成して内容を書き込む
        void CreateSharedIndexBuffer(ID3D12Device* device);

        /// @brief 溜まっている頂点を 1 ドローコールで描く
        void Flush();

        /// @brief 深度モードとブレンドモードから PSO を引く
        ID3D12PipelineState* ResolvePipelineState(Text3DDepthMode depthMode, BlendMode blendMode) const;

        GraphicsCore* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;

        /// 深度テストを切った PSO（オーバーレイ描画用）。psoMg_ が深度テスト版
        std::unique_ptr<PipelineStateManager> overlayPsoMg_ = std::make_unique<PipelineStateManager>();

        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        // ── バッチ状態 ────────────────────────────────────────────
        // フォント（アトラスと定数）・深度モード（PSO）・ビュー射影（定数）の
        // どれかが変わるとバッチを切る
        /// 記録先。BeginPass で受け取り、EndPass のフラッシュでも使う
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        const MsdfFont* batchFont_ = nullptr;
        Text3DDepthMode batchDepthMode_ = Text3DDepthMode::Test;
        Matrix4x4 batchViewProjection_{};
        std::vector<Text3DVertex> batchVertices_;

        uint32_t frameDrawCalls_ = 0;
        uint32_t frameGlyphs_ = 0;
        uint32_t lastFrameDrawCalls_ = 0;
        uint32_t lastFrameGlyphs_ = 0;
    };
}

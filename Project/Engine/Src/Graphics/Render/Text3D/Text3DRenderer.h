#pragma once

#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Text/MsdfTextBatch.h"

#include <cstdint>
#include <d3d12.h>
#include <string>

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

    /// @brief MSDF テキストをワールド空間へ描くレンダラー
    /// @details
    ///  UI 版（TextRenderer）との違いは、頂点へ焼き込む行列がスクリーン px ではなく
    ///  ワールド行列であることと、定数バッファへ渡す射影がカメラのビュー射影であること。
    ///  距離場そのものと、アンチエイリアスの効かせ方（fwidth からの画面 px 換算）は
    ///  そのまま流用できるので、ピクセルシェーダーは UI 版とほぼ同一。
    ///
    ///  **バッチング**:
    ///  `Text3DRendererComponent::Render` はドローコールを発行せず Submit() で頂点を積むだけ。
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
        static constexpr uint32_t kMaxGlyphsPerBatch = MsdfTextBatch::kMaxGlyphsPerBatch;

        /// @brief 1 つのテキストが描けるグリフ数の上限
        static constexpr uint32_t kMaxGlyphsPerText = kMaxGlyphsPerBatch;

        // ===== IRenderer インターフェース =====
        RenderPassType GetRenderPassType() const override { return RenderPassType::Text3D; }

        /// @brief PSO / ルートシグネチャと共有インデックスバッファを作る
        void Initialize(ID3D12Device* device) override;

        /// @brief 初期化（GraphicsCore 付き。こちらを使うこと）
        void Initialize(GraphicsCore* dxCommon);

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
            const TextDrawStyle& style,
            Text3DDepthMode depthMode);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief GraphicsCore を取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }

        // ===== デバッグ表示用 =====
        /// @brief 直近フレームのドローコール数（バッチングの効きを見る）
        uint32_t GetLastFrameDrawCallCount() const { return batch_.GetLastFrameDrawCallCount(); }
        /// @brief 直近フレームに描いたグリフ数
        uint32_t GetLastFrameGlyphCount() const { return batch_.GetLastFrameGlyphCount(); }

    private:
        /// @brief 溜まっている頂点を 1 ドローコールで描く
        void Flush();

        /// @brief 深度モードとブレンドモードから PSO を引く
        ID3D12PipelineState* ResolvePipelineState(Text3DDepthMode depthMode, BlendMode blendMode) const;

        GraphicsCore* dxCommon_ = nullptr;

        /// ルートシグネチャから引いたシェーダーの結線
        BindingTable bindings_;

        /// 深度テストを切った PSO（オーバーレイ描画用）。psoMg_ が深度テスト版
        std::unique_ptr<PipelineStateManager> overlayPsoMg_ = std::make_unique<PipelineStateManager>();

        // ── バッチ状態 ────────────────────────────────────────────
        // フォント（アトラスと定数）・深度モード（PSO）・ビュー射影（定数）の
        // どれかが変わるとバッチを切る
        /// 記録先。BeginPass で受け取り、EndPass のフラッシュでも使う
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
        Text3DDepthMode batchDepthMode_ = Text3DDepthMode::Test;
        Matrix4x4 batchViewProjection_{};
        /// 積んでいる頂点とバッチのフォント
        MsdfTextBatch batch_;
    };
}

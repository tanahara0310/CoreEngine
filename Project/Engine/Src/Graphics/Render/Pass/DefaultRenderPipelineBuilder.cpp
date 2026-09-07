#include "pch.h"
#include "DefaultRenderPipelineBuilder.h"

#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"

// レンダーパス（具象型を知るのはこの翻訳単位だけ）
#include "Graphics/Render/Pass/ASBuildPass.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/HiZOcclusionPass.h"
#include "Graphics/Render/Pass/SSAOPass.h"
#include "Graphics/Render/Pass/TAAPass.h"
#include "Graphics/Render/Pass/CASPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/RTShadowPass.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/Pass/RTWaterRefractionPass.h"
#include "Graphics/Render/Pass/RTWaterReflectionPass.h"
#include "Graphics/Render/Pass/FFTOceanPass.h"
#include "Graphics/Render/Pass/AtmosphereLUTPass.h"
#include "Graphics/Render/Pass/AerialPerspectivePass.h"
#include "Graphics/Render/Pass/CloudShadowMapPass.h"
#include "Graphics/Render/Pass/VolumetricCloudNoisePass.h"
#include "Graphics/Render/Pass/VolumetricCloudPass.h"
#include "Graphics/Render/Pass/GodRayPass.h"
#include "Graphics/Render/Pass/FogPass.h"
#include "Graphics/Render/Pass/WaterCausticsPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/Render/Pass/SceneColorCopyPass.h"
#include "Graphics/Render/Pass/WaterSurfacePass.h"
#include "Graphics/Render/Pass/PostEffectPass.h"
#include "Graphics/Render/Pass/BackBufferPass.h"

namespace CoreEngine
{
    void DefaultRenderPipelineBuilder::Build(RenderPipeline& pipeline, HiZOcclusionSystem* hiZOcclusion)
    {
        // 各パスは (フェーズ, フェーズ内優先度) で登録する。
        // 実行順・バリアは各パスの DeclareResources 宣言から RenderGraph が導出する。

        // フレーム前処理: DXR 加速構造構築（frameNumber ガードでフレーム内 1 回のみ実行）
        pipeline.AddPass(std::make_unique<ASBuildPass>(), RenderPassPhase::FrameSetup);

        // フレーム前処理: 大気散乱 LUT 生成（パラメータ変更時のみ Compute 実行）
        // パス自身が SRV ヒープをバインドするため、フレーム先頭でも安全に実行できる
        pipeline.AddPass(std::make_unique<AtmosphereLUTPass>(), RenderPassPhase::FrameSetup, 10);

        // フレーム前処理: ボリューメトリック雲のノイズ生成（ダーティ時のみ Compute 実行）
        pipeline.AddPass(std::make_unique<VolumetricCloudNoisePass>(), RenderPassPhase::FrameSetup, 20);

        // 雲シャドウマップ: Deferred ライティングとゴッドレイの双方が読むので
        // ライティングより前のこのフェーズで生成する（GameView のみ）
        pipeline.AddPass(std::make_unique<CloudShadowMapPass>(), RenderPassPhase::FrameSetup, 30);

        // G-Buffer 蓄積（不透明 Model / SkinnedModel の描画）
        pipeline.AddPass(std::make_unique<GBufferPass>(), RenderPassPhase::GBuffer);

        // G-Buffer 完成直後: Hi-Z ピラミッド構築 + 遮蔽判定（メイン GameView のみ。
        // 結果はフレームリング一巡後の Model::Draw が Submit スキップに使う）
        pipeline.AddPass(std::make_unique<HiZOcclusionPass>(hiZOcclusion), RenderPassPhase::PreLighting, 5);

        // ライティング前処理: SSAO / RT シャドウ / コースティクス
        auto ssaoPass = std::make_unique<SSAOPass>();
        ssaoPass->SetSSAOTargetName(RenderTargetNames::SSAOBuffer);
        ssaoPass->SetSSAOBlurTargetName(RenderTargetNames::SSAOBlurBuffer);
        pipeline.AddPass(std::move(ssaoPass), RenderPassPhase::PreLighting, 0);
        pipeline.AddPass(std::make_unique<RTShadowPass>(), RenderPassPhase::PreLighting, 10);
        pipeline.AddPass(std::make_unique<RTShadowTemporalPass>(), RenderPassPhase::PreLighting, 11);
        pipeline.AddPass(std::make_unique<RTShadowDenoisePass>(), RenderPassPhase::PreLighting, 12);
        // コースティクスは実行順の都合で PreLighting だが、コストの分類は水面。
        // ここを Water へ寄せないと「水面がフレームに占める割合」から集光分が抜け落ちる。
        pipeline.AddPass(
            std::make_unique<RTWaterCausticsPass>(), RenderPassPhase::PreLighting, 20,
            GpuTimingCategory::Water);
        pipeline.AddPass(
            std::make_unique<WaterCausticsPass>(), RenderPassPhase::PreLighting, 30,
            GpuTimingCategory::Water);

        // Deferred ライティング: G-Buffer を読み取り SceneColor を生成
        pipeline.AddPass(std::make_unique<DeferredLightingPass>(), RenderPassPhase::Lighting);

        // ライティング後: FFT 波面更新と空気遠近感の合成（GameView のみ）
        // 波面生成は実行順の都合で PostLighting に置いているが、コストの分類としては水面。
        // 計測カテゴリを Water へ寄せないと「水面がフレームに占める割合」を数え漏らす。
        pipeline.AddPass(
            std::make_unique<FFTOceanPass>(), RenderPassPhase::PostLighting, 0,
            GpuTimingCategory::Water);
        pipeline.AddPass(std::make_unique<AerialPerspectivePass>(), RenderPassPhase::PostLighting, 10);

        // Forward 合成（従来の大箱 GeometryPass をキュー単位の 3 パスへ分割）
        // 不透明 Model/SkinnedModel は投入時に GBuffer 経路へ振り分け済みなので含まれない
        pipeline.AddPass(std::make_unique<GeometryPass>(), RenderPassPhase::Sky, 0);
        pipeline.AddPass(std::make_unique<SkyBoxQueuePass>(), RenderPassPhase::Sky, 10);

        // ボリューメトリック雲: SkyBox 描画後の SceneColor へレイマーチ結果を合成（GameView のみ）
        pipeline.AddPass(std::make_unique<VolumetricCloudPass>(), RenderPassPhase::Sky, 20);

        // ゴッドレイ: 雲シャドウマップ生成 + 内散乱の遮蔽差分を SceneColor へ合成（GameView のみ）
        pipeline.AddPass(std::make_unique<GodRayPass>(), RenderPassPhase::Sky, 30);

        // 高さフォグ: 空・雲・ゴッドレイまで載った SceneColor へフォグを合成（GameView のみ）
        // 背景（穴・世界の縁）も対象にするため SkyBox 描画より後、半透明より前に置く
        pipeline.AddPass(std::make_unique<FogPass>(), RenderPassPhase::Sky, 40);

        pipeline.AddPass(std::make_unique<TransparentQueuePass>(), RenderPassPhase::Transparent, 0);

        // 水面: 背景 SceneColor の複製 → RT 屈折 → RT 反射 → 水面合成（データフロー順）
        pipeline.AddPass(std::make_unique<SceneColorCopyPass>(), RenderPassPhase::Water, 0);
        pipeline.AddPass(std::make_unique<RTWaterRefractionPass>(), RenderPassPhase::Water, 10);
        pipeline.AddPass(std::make_unique<RTWaterReflectionPass>(), RenderPassPhase::Water, 15);
        pipeline.AddPass(std::make_unique<WaterSurfacePass>(), RenderPassPhase::Water, 20);

        // TAA: トーンマップ前の HDR 空間で解決する（ポストエフェクト列より必ず前）
        pipeline.AddPass(std::make_unique<TAAPass>(), RenderPassPhase::PostProcess, 0);

        // CAS: TAA でぼけた分のシャープ化。必ず TAA の直後
        pipeline.AddPass(std::make_unique<CASPass>(), RenderPassPhase::PostProcess, 5);

        // ポストエフェクト（有効エフェクト列は Graph 構築時にノード分解される）
        pipeline.AddPass(std::make_unique<PostEffectPass>(), RenderPassPhase::PostProcess, 10);

        // バックバッファへの最終出力
        auto backBufferPass = std::make_unique<BackBufferPass>();
        backBufferPass->SetRenderTargetName(RenderTargetNames::BackBuffer);  // 名前ベースで指定
        pipeline.AddPass(std::move(backBufferPass), RenderPassPhase::Final);
    }
}

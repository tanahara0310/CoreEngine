#include "RenderManager.h"
#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Render/Particle/ParticleRenderer.h"
#include "Engine/Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Engine/Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Engine/Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Engine/Graphics/Shadow/ShadowMapManager.h"
#include "Engine/Graphics/Model/Model.h"
#include "Engine/TestGameObject/SkyBoxObject.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Math/MathCore.h"
#include "Engine/Graphics/Render/Render.h"
#include <algorithm>


namespace CoreEngine
{
    void RenderManager::Initialize(ID3D12Device* device) {
        // 現時点では特に初期化処理なし
        (void)device; // 未使用警告を回避
    }

    void RenderManager::RegisterRenderer(RenderPassType type, std::unique_ptr<IRenderer> renderer) {
        renderers_[type] = std::move(renderer);
        
        // ModelRendererが登録された場合、Model クラスに設定
        if (type == RenderPassType::Model) {
            auto* modelRenderer = dynamic_cast<ModelRenderer*>(renderers_[type].get());
            if (modelRenderer) {
                Model::SetModelRenderer(modelRenderer);
            }
        }

        // SkinnedModelRendererが登録された場合、Model クラスに設定
        if (type == RenderPassType::SkinnedModel) {
            auto* skinnedModelRenderer = dynamic_cast<SkinnedModelRenderer*>(renderers_[type].get());
            if (skinnedModelRenderer) {
                Model::SetSkinnedModelRenderer(skinnedModelRenderer);
            }
        }

        // SkyBoxRendererが登録された場合、SkyBoxObject クラスに設定
        if (type == RenderPassType::SkyBox) {
            auto* skyBoxRenderer = dynamic_cast<SkyBoxRenderer*>(renderers_[type].get());
            if (skyBoxRenderer) {
                SkyBoxObject::SetSkyBoxRenderer(skyBoxRenderer);
            }
        }

        // ShadowMapRendererが登録された場合、Model クラスに設定
        if (type == RenderPassType::ShadowMap) {
            auto* shadowMapRenderer = dynamic_cast<ShadowMapRenderer*>(renderers_[type].get());
            if (shadowMapRenderer) {
                Model::SetShadowMapRenderer(shadowMapRenderer);
            }
        }
    }

    IRenderer* RenderManager::GetRenderer(RenderPassType type) {
        auto it = renderers_.find(type);
        if (it != renderers_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RenderManager::SetCameraManager(CameraManager* cameraManager) {
        cameraManager_ = cameraManager;
    }

    void RenderManager::SetCamera(const ICamera* camera) {
        camera_ = camera;

        // 各レンダラーにもカメラを設定（互換性維持）
        for (auto& [type, renderer] : renderers_) {
            renderer->SetCamera(camera);
        }
    }

    void RenderManager::SetCommandList(ID3D12GraphicsCommandList* cmdList) {
        cmdList_ = cmdList;
    }

    void RenderManager::SetShadowMapManager(ShadowMapManager* shadowMapManager) {
        shadowMapManager_ = shadowMapManager;
    }

    void RenderManager::SetRender(Render* render) {
        render_ = render;
    }

    void RenderManager::SetLightViewProjection(const Matrix4x4& lightViewProjection) {
        // ShadowMapManagerに委譲（一元管理）
        if (shadowMapManager_) {
            shadowMapManager_->SetLightViewProjection(lightViewProjection);
        }
    }

    void RenderManager::AddDrawable(GameObject* obj) {
        // GameObjectManagerで事前フィルタリング済み（null/Active/MarkedForDestroyチェック済み）
        DrawCommand cmd;
        cmd.object = obj;
        cmd.passType = obj->GetRenderPassType();
        cmd.blendMode = obj->GetBlendMode();

        drawQueue_.push_back(cmd);
    }

    const ICamera* RenderManager::GetCameraForPass(RenderPassType passType) {
        // カメラマネージャーがある場合はタイプ別のカメラを取得
        if (cameraManager_) {
            // 2D描画パス（Sprite、Text）は2Dカメラを使用
            if (passType == RenderPassType::Sprite || passType == RenderPassType::Text) {
                return cameraManager_->GetActiveCamera(CameraType::Camera2D);
            }
            // その他は3Dカメラを使用
            else {
                return cameraManager_->GetActiveCamera(CameraType::Camera3D);
            }
        }

        // カメラマネージャーがない場合は従来のカメラを使用
        return camera_;
    }

    void RenderManager::DrawAll() {
        if (drawQueue_.empty()) {
#ifdef _DEBUG
            // 描画キューが空の場合は警告を出力（通常は問題ないが、意図しない場合に気づくため）
            static bool firstWarning = true;
            if (firstWarning) {
                OutputDebugStringA("WARNING: RenderManager draw queue is empty in DrawAll().\n");
                firstWarning = false;
            }
#endif
            return;
        }

        if (!cmdList_) {
#ifdef _DEBUG
            OutputDebugStringA("ERROR: CommandList is null in RenderManager::DrawAll!\n");
#endif
            return;
        }

        SortDrawQueue();

        // === Phase 1: シャドウマップパス ===
        if (shadowMapManager_) {
            RenderShadowMapPass();

            // シャドウマップパスの後、リソースバリアを実行
            // DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
            shadowMapManager_->TransitionToShaderResource(cmdList_);

            // 通常描画用のRTV/DSVを復元
            if (render_) {
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = render_->GetOffscreenRTVHandle(0);
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = render_->GetDSVHandle();
                cmdList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

                // ビューポートとシザー矩形を復元
                D3D12_VIEWPORT viewport = render_->GetViewport();
                D3D12_RECT scissorRect = render_->GetScissorRect();
                cmdList_->RSSetViewports(1, &viewport);
                cmdList_->RSSetScissorRects(1, &scissorRect);
            }

            // ModelRendererにシャドウマップを設定
            if (auto* modelRenderer = static_cast<ModelRenderer*>(GetRenderer(RenderPassType::Model))) {
                modelRenderer->SetShadowMap(shadowMapManager_->GetSRVHandle());
            }
            if (auto* skinnedRenderer = static_cast<SkinnedModelRenderer*>(GetRenderer(RenderPassType::SkinnedModel))) {
                skinnedRenderer->SetShadowMap(shadowMapManager_->GetSRVHandle());
            }
        }

        // === Phase 2: 通常描画パス ===
        RenderNormalPass();
    }

    void RenderManager::RenderShadowMapPass() {
        // シャドウマップ用のレンダラーを取得
        IRenderer* renderer = GetRenderer(RenderPassType::ShadowMap);
        if (!renderer) {
            return; // シャドウマップレンダラーが登録されていない
        }

        // 型の安全チェック
        auto* shadowMapRenderer = static_cast<ShadowMapRenderer*>(renderer);
        assert(shadowMapRenderer && "ShadowMapRenderer type mismatch!");

        // リソースバリア: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
        shadowMapManager_->TransitionToDepthWrite(cmdList_);

        // シャドウマップをクリア
        shadowMapManager_->ClearDepth(cmdList_);

        // DSVを設定（RTVはなし）
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = shadowMapManager_->GetDSVHandle();
        cmdList_->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);

        // ビューポートとシザー矩形を設定
        UINT shadowMapSize = shadowMapManager_->GetShadowMapSize();
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<FLOAT>(shadowMapSize);
        viewport.Height = static_cast<FLOAT>(shadowMapSize);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList_->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect = {};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = static_cast<LONG>(shadowMapSize);
        scissorRect.bottom = static_cast<LONG>(shadowMapSize);
        cmdList_->RSSetScissorRects(1, &scissorRect);

        // ライトVP行列を設定（ShadowMapManagerから取得）
        shadowMapRenderer->SetLightViewProjection(shadowMapManager_->GetLightViewProjection());

        // シャドウマップパスを開始
        shadowMapRenderer->BeginPass(cmdList_, BlendMode::kBlendModeNone);

        // モデルとスキニングモデルのみを描画
        int drawCount = 0;
        for (const auto& cmd : drawQueue_) {
            if (!cmd.object || cmd.object->IsMarkedForDestroy()) {
                continue;
            }

            // モデルまたはスキニングモデルのみシャドウを生成
            if (cmd.passType == RenderPassType::Model) {
                shadowMapRenderer->SetPSOForNormalModel();
                cmd.object->DrawShadow(cmdList_);
                drawCount++;
            } else if (cmd.passType == RenderPassType::SkinnedModel) {
                shadowMapRenderer->SetPSOForSkinnedModel();
                cmd.object->DrawShadow(cmdList_);
                drawCount++;
            }
        }

        // シャドウマップパスを終了
        shadowMapRenderer->EndPass();
    }


    void RenderManager::RenderNormalPass() {
        RenderPassType currentPass = RenderPassType::Invalid;
        IRenderer* currentRenderer = nullptr;
        const ICamera* currentCamera = nullptr;

        for (const auto& cmd : drawQueue_) {
            // GameObjectManagerで事前フィルタリング済み
            // 削除マークのみチェック（更新中に削除マークされた可能性があるため）
            if (!cmd.object || cmd.object->IsMarkedForDestroy()) {
                continue;
            }

            // パスが切り替わったら処理
            if (cmd.passType != currentPass) {
                // 前のパスを終了
                if (currentRenderer) {
                    currentRenderer->EndPass();
                }

                // 新しいパスを開始
                currentPass = cmd.passType;
                auto it = renderers_.find(currentPass);
                if (it != renderers_.end()) {
                    currentRenderer = it->second.get();

                    // パスに応じたカメラを取得
                    currentCamera = GetCameraForPass(currentPass);
                    currentRenderer->SetCamera(currentCamera);

                    currentRenderer->BeginPass(cmdList_, cmd.blendMode);
                } else {
#ifdef _DEBUG
                    OutputDebugStringA("WARNING: Renderer not found for pass type!\n");
#endif
                    currentRenderer = nullptr;
                    currentCamera = nullptr;
                }
            }

            // オブジェクトを描画
            if (currentRenderer) {
                cmd.object->Draw(currentCamera);

                // パーティクルの場合は、レンダラーに描画コマンド発行を委託
                if (cmd.passType == RenderPassType::Particle) {
                    if (auto* particleRenderer = static_cast<ParticleRenderer*>(currentRenderer)) {
                        auto* particleSystem = static_cast<ParticleSystem*>(cmd.object);
                        particleRenderer->Draw(particleSystem);
                    }
                }
                // モデルパーティクルの場合
                else if (cmd.passType == RenderPassType::ModelParticle) {
                    if (auto* modelParticleRenderer = static_cast<ModelParticleRenderer*>(currentRenderer)) {
                        auto* particleSystem = static_cast<ParticleSystem*>(cmd.object);
                        modelParticleRenderer->Draw(particleSystem);
                    }
                }
            }
        }

        // 最後のパスを終了
        if (currentRenderer) {
            currentRenderer->EndPass();
        }
    }

    void RenderManager::ClearQueue() {
        drawQueue_.clear();
    }

    void RenderManager::SortDrawQueue() {
        // 描画コマンドを最適化してステート変更を最小化
        // 優先順位: 1. パスタイプ > 2. ブレンドモード
        std::sort(drawQueue_.begin(), drawQueue_.end(),
            [](const DrawCommand& a, const DrawCommand& b) {
                // 1. パスタイプでソート（パイプライン切り替え最小化）
                if (a.passType != b.passType) {
                    return static_cast<int>(a.passType) < static_cast<int>(b.passType);
                }

                // 2. 同一パス内ではブレンドモードでソート（ブレンドステート切り替え最小化）
                if (a.blendMode != b.blendMode) {
                    return static_cast<int>(a.blendMode) < static_cast<int>(b.blendMode);
                }

                // 3. その他の条件が同じ場合は順序を維持（安定ソート）
                return false;
            });
    }
}

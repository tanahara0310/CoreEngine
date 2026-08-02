#include "pch.h"
#include "RenderManager.h"
#include "IGBufferRenderer.h"
#include "GameObject/GameObject.h"
#include "Particle/ParticleSystem.h"
#include "Particle/Gpu/GpuParticleSystem.h"
#include "Graphics/Render/Particle/ParticleRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Graphics/Render/Particle/GpuParticleRenderer.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Model/IBLParameters.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "GameObjects/SkyBox/SkyBoxObject.h"
#include "Camera/Camera.h"
#include "Camera/View/ViewInfo.h"
#include "Math/MathCore.h"
#include <algorithm>


namespace CoreEngine
{
    void RenderManager::Initialize(ID3D12Device* device) {
        // 描画パスタイプのデフォルト優先度を設定
        ResetPassTypePriorities();
        (void)device; // 未使用警告を回避
    }

    void RenderManager::RegisterRenderer(RenderPassType type, std::unique_ptr<IRenderer> renderer) {
        renderers_[type] = std::move(renderer);

        // SkyBoxRendererが登録された場合、SkyBoxObject クラスに設定
        if (type == RenderPassType::SkyBox) {
            auto* skyBoxRenderer = dynamic_cast<SkyBoxRenderer*>(renderers_[type].get());
            if (skyBoxRenderer) {
                SkyBoxObject::SetSkyBoxRenderer(skyBoxRenderer);
            }
        }

        if (type == RenderPassType::Model || type == RenderPassType::SkinnedModel) {
            ApplyEnvironmentLightingToRenderers();
        }
    }

    IRenderer* RenderManager::GetRenderer(RenderPassType type) {
        auto it = renderers_.find(type);
        if (it != renderers_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RenderManager::SetIBLRotation(const Vector3& rotation) {
        iblRotation_ = rotation;
        ApplyEnvironmentLightingToRenderers();
    }

    void RenderManager::SetEnvironmentIntensity(float intensity) {
        environmentIntensity_ = intensity;
        ApplyEnvironmentLightingToRenderers();
    }

    void RenderManager::SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle) {
        environmentMapHandle_ = environmentMapHandle;
        ApplyEnvironmentLightingToRenderers();
    }

    void RenderManager::SetIBLMaps(
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle) {
        irradianceMapHandle_ = irradianceHandle;
        prefilteredMapHandle_ = prefilteredHandle;
        brdfLUTHandle_ = brdfLUTHandle;
        ApplyEnvironmentLightingToRenderers();
    }

    void RenderManager::SetCommandList(ID3D12GraphicsCommandList* cmdList) {
        cmdList_ = cmdList;
    }

    void RenderManager::AddRenderItem(RenderItem item) {
        item.registrationOrder = registrationCounter_++;
        item.sortKey = ResolveRenderOrder(item);

        if (item.kind == RenderItemKind::WaterSurface) {
            waterDrawQueue_.push_back(std::move(item));
        } else if (item.kind == RenderItemKind::SkyBox) {
            skyDrawQueue_.push_back(std::move(item));
        } else if (item.kind == RenderItemKind::Transparent) {
            transparentDrawQueue_.push_back(std::move(item));
        } else {
            // 不透明の Model/SkinnedModel は投入時に Deferred（GBuffer）経路へ振り分ける。
            // それ以外（パーティクル・ライン・スプライト・透過ブレンド等）は Forward 経路。
            const bool isOpaqueModel =
                (item.passType == RenderPassType::Model || item.passType == RenderPassType::SkinnedModel)
                && item.blendMode == BlendMode::kBlendModeNone;
            if (isOpaqueModel) {
                opaqueDrawQueue_.push_back(std::move(item));
            } else {
                drawQueue_.push_back(std::move(item));
            }
        }

        isQueueSorted_ = false;
    }

    const ViewInfo* RenderManager::GetViewForPass(RenderPassType passType, RenderViewType viewType) const {
        if (!frameViews_) {
            return nullptr;
        }

        // UI はスクリーン固定座標でビュー非依存
        if (passType == RenderPassType::UI) {
            return nullptr;
        }

        const ViewInfo& view = (passType == RenderPassType::Sprite)
            ? frameViews_->View2D()
            : frameViews_->Get(viewType);

        return view.isValid ? &view : nullptr;
    }

    void RenderManager::DrawGBufferPass(RenderViewType viewType) {
        if (opaqueDrawQueue_.empty() || !cmdList_) {
            return;
        }

        EnsureQueueSorted();

        // BeginGBufferPass() は IGBufferRenderer を実装するレンダラーのみが対応する
        IRenderer* modelRenderer   = GetRenderer(RenderPassType::Model);
        IRenderer* skinnedRenderer = GetRenderer(RenderPassType::SkinnedModel);
        auto* modelGBuffer   = dynamic_cast<IGBufferRenderer*>(modelRenderer);
        auto* skinnedGBuffer = dynamic_cast<IGBufferRenderer*>(skinnedRenderer);
        const ViewInfo* currentView = GetViewForPass(RenderPassType::Model, viewType);
        const Camera* currentCamera = currentView ? currentView->camera : nullptr;

        if (modelRenderer) {
            modelRenderer->SetCamera(currentCamera);
        }
        if (skinnedRenderer) {
            skinnedRenderer->SetCamera(currentCamera);
        }

        IRenderer* activeRenderer = nullptr;
        RenderPassType activePass = RenderPassType::Invalid;

        // 投入時に GBuffer 経路へ振り分けられた不透明 Model/SkinnedModel のみを描画する。
        for (const auto& cmd : opaqueDrawQueue_) {
            if (!cmd.object || cmd.object->IsMarkedForDestroy()) {
                continue;
            }

            if (cmd.passType != activePass) {
                if (activeRenderer) {
                    activeRenderer->EndPass();
                }

                activePass = cmd.passType;
                activeRenderer = nullptr;

                if (cmd.passType == RenderPassType::Model && modelGBuffer) {
                    modelGBuffer->BeginGBufferPass(cmdList_);
                    activeRenderer = modelRenderer;
                } else if (cmd.passType == RenderPassType::SkinnedModel && skinnedGBuffer) {
                    skinnedGBuffer->BeginGBufferPass(cmdList_);
                    activeRenderer = skinnedRenderer;
                }
            }

            if (activeRenderer) {
                DrawViewInfo view{};
                view.view = currentView;
                view.viewType = viewType;
                view.isGBufferPass = true;
                cmd.object->Draw(view);
            }
        }

        if (activeRenderer) {
            activeRenderer->EndPass();
        }
    }

    void RenderManager::DrawGeometryPass(RenderViewType viewType) {
        if ((drawQueue_.empty() && skyDrawQueue_.empty() && transparentDrawQueue_.empty() && waterDrawQueue_.empty()) || !cmdList_) {
            return;
        }

        EnsureQueueSorted();

        DrawMainQueuePass(viewType);
        DrawSkyQueuePass(viewType);
        DrawTransparentQueuePass(viewType);
        DrawWaterQueuePass(viewType);
    }

    void RenderManager::DrawMainQueuePass(RenderViewType viewType) {
        if ((drawQueue_.empty() && (deferredLightingActive_ || opaqueDrawQueue_.empty())) || !cmdList_) {
            return;
        }

        EnsureQueueSorted();

        // Deferred 経路が無効な場合のみ、不透明キューを Forward でフォールバック描画する。
        if (!deferredLightingActive_ && !opaqueDrawQueue_.empty()) {
            RenderNormalPassQueue(opaqueDrawQueue_, viewType);
        }

        RenderNormalPassQueue(drawQueue_, viewType);
    }

    void RenderManager::DrawWaterQueuePass(RenderViewType viewType) {
        // 水面は GameView 限定。反射ビューで描くと水面が自分の平面反射に
        // 描き込まれる（夜の大きな明暗斑バグの原因）。WaterSurfacePass 側の
        // IsEnabledForView と同じ制約をキュー層でも二重に守る
        // （DrawGeometryPass 経由など、パスを通らない呼び出し経路への防壁）。
        if (viewType != RenderViewType::GameView) {
            return;
        }
        if (waterDrawQueue_.empty() || !cmdList_) {
            return;
        }

        EnsureQueueSorted();
        RenderNormalPassQueue(waterDrawQueue_, viewType);
    }

    void RenderManager::DrawSkyQueuePass(RenderViewType viewType) {
        if (skyDrawQueue_.empty() || !cmdList_) {
            return;
        }

        EnsureQueueSorted();
        RenderNormalPassQueue(skyDrawQueue_, viewType);
    }

    void RenderManager::DrawTransparentQueuePass(RenderViewType viewType) {
        if (transparentDrawQueue_.empty() || !cmdList_) {
            return;
        }

        EnsureQueueSorted();
        RenderNormalPassQueue(transparentDrawQueue_, viewType);
    }

    void RenderManager::ApplyEnvironmentLightingToRenderers() {
        // IBL パラメータを構造体にまとめて一括適用
        IBLParameters params;
        params.environmentMap = environmentMapHandle_;
        params.irradianceMap = irradianceMapHandle_;
        params.prefilteredMap = prefilteredMapHandle_;
        params.brdfLUT = brdfLUTHandle_;
        params.rotation = iblRotation_;
        params.intensity = environmentIntensity_;

        // Model / SkinnedModel の両レンダラーに適用
        for (auto passType : {RenderPassType::Model, RenderPassType::SkinnedModel}) {
            if (auto* renderer = dynamic_cast<BaseModelRenderer*>(GetRenderer(passType))) {
                renderer->SetIBLParameters(params);
            }
        }
    }


    IRenderer* RenderManager::ResolveRendererForPass(RenderPassType passType) {
        if (IRenderer* renderer = GetRenderer(passType)) {
            return renderer;
        }

        if (passType == RenderPassType::WaterSurface) {
            return GetRenderer(RenderPassType::Model);
        }

        return nullptr;
    }

    void RenderManager::RenderNormalPassQueue(const std::vector<RenderItem>& queue, RenderViewType viewType) {
        RenderPassType currentPass = RenderPassType::Invalid;
        BlendMode currentBlendMode = BlendMode::kBlendModeNone;
        IRenderer* currentRenderer = nullptr;
        const ViewInfo* currentView = nullptr;

        for (const auto& cmd : queue) {
            // GameObjectManagerで事前フィルタリング済み
            // 削除マークのみチェック（更新中に削除マークされた可能性があるため）
            if (!cmd.object || cmd.object->IsMarkedForDestroy()) {
                continue;
            }

            if (!renderDebugLines_ && cmd.passType == RenderPassType::Line) {
                continue;
            }

            // 不透明 Model/SkinnedModel は AddRenderItem 時点で opaqueDrawQueue_（Deferred 経路）へ
            // 振り分け済みのため、ここでのスキップ判定は不要。

            const bool passChanged = cmd.passType != currentPass;
            const bool blendChanged = cmd.blendMode != currentBlendMode;

            // パスが切り替わったら処理
            if (passChanged) {
                // 前のパスを終了
                if (currentRenderer) {
                    currentRenderer->EndPass();
                }

                // 新しいパスを開始
                currentPass = cmd.passType;
                currentBlendMode = cmd.blendMode;
                currentRenderer = ResolveRendererForPass(currentPass);
                if (currentRenderer) {

                    // パスに応じたビューを取得
                    currentView = GetViewForPass(currentPass, viewType);
                    currentRenderer->SetCamera(currentView ? currentView->camera : nullptr);

                    currentRenderer->BeginPass(cmdList_, cmd.blendMode);
                } else {
#ifdef _DEBUG
                    OutputDebugStringA("WARNING: Renderer not found for pass type!\n");
#endif
                    currentRenderer = nullptr;
                    currentView = nullptr;
                }
            } else if (blendChanged && currentRenderer) {
                // 同一パス内でブレンドモードが変わった場合はPSOを切り替え
                currentBlendMode = cmd.blendMode;
                currentRenderer->BeginPass(cmdList_, cmd.blendMode);
            }

            // オブジェクトを描画
            if (currentRenderer) {
                DrawViewInfo view{};
                view.view = currentView;
                view.viewType = viewType;
                view.isGBufferPass = false;
                cmd.object->Draw(view);

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
                // GPUパーティクルの場合（CSディスパッチ + 描画をレンダラーに委託）
                else if (cmd.passType == RenderPassType::GpuParticle) {
                    if (auto* gpuParticleRenderer = static_cast<GpuParticleRenderer*>(currentRenderer)) {
                        auto* gpuParticleSystem = static_cast<GpuParticleSystem*>(cmd.object);
                        gpuParticleRenderer->DrawGpu(gpuParticleSystem);
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
        opaqueDrawQueue_.clear();
        skyDrawQueue_.clear();
        transparentDrawQueue_.clear();
        waterDrawQueue_.clear();
        registrationCounter_ = 0;
        isQueueSorted_ = false;
    }

    void RenderManager::SetPassTypePriority(RenderPassType type, int priority) {
        passTypePriorities_[type] = priority;
    }

    int RenderManager::GetPassTypePriority(RenderPassType type) const {
        auto it = passTypePriorities_.find(type);
        if (it != passTypePriorities_.end()) {
            return it->second;
        }
        // 未登録パスタイプはデフォルトとして enum 値 × 100 を返す
        return static_cast<int>(type) * 100;
    }

    void RenderManager::ResetPassTypePriorities() {
        // デフォルト優先度（間隔 100 でユーザーが中間値を挿入しやすくする）
        passTypePriorities_[RenderPassType::Model] = 100;
        passTypePriorities_[RenderPassType::SkinnedModel] = 200;
        passTypePriorities_[RenderPassType::SkyBox] = 300;
        passTypePriorities_[RenderPassType::WaterSurface] = 350;
        passTypePriorities_[RenderPassType::ModelParticle] = 400;
        passTypePriorities_[RenderPassType::Line] = 500;
        passTypePriorities_[RenderPassType::Particle] = 600;
        passTypePriorities_[RenderPassType::GpuParticle] = 650;
        passTypePriorities_[RenderPassType::Sprite] = 700;
        passTypePriorities_[RenderPassType::UI] = 800;       // UI は常に最後（最前面）
    }

    void RenderManager::EnsureQueueSorted() {
        if (!isQueueSorted_) {
            SortDrawQueue();
            isQueueSorted_ = true;
        }
    }

    void RenderManager::SortDrawQueue() {
        SortRenderQueue(drawQueue_);
        SortRenderQueue(opaqueDrawQueue_);
        SortRenderQueue(skyDrawQueue_);
        SortRenderQueue(transparentDrawQueue_);
        SortRenderQueue(waterDrawQueue_);
    }

    void RenderManager::SortRenderQueue(std::vector<RenderItem>& queue) {
        // 優先順位:
        //   1. sortKey      : RenderItem に確定済みの描画順キー（小さいほど先に描画）
        //   2. passType     : 同一 sortKey 内でレンダラー切り替えを最小化
        //   3. blendMode    : 同一パス内でブレンドステート切り替えを最小化
        //   4. registrationOrder : 同一条件内では登録順序を維持
        std::stable_sort(queue.begin(), queue.end(),
            [](const RenderItem& a, const RenderItem& b) {
                if (a.sortKey != b.sortKey) {
                    return a.sortKey < b.sortKey;
                }
                if (a.passType != b.passType) {
                    return static_cast<int>(a.passType) < static_cast<int>(b.passType);
                }
                if (a.blendMode != b.blendMode) {
                    return static_cast<int>(a.blendMode) < static_cast<int>(b.blendMode);
                }
                return a.registrationOrder < b.registrationOrder;
            });
    }

    int RenderManager::ResolveRenderOrder(const RenderItem& item) const {
        if (item.renderOrderOverride) {
            return *item.renderOrderOverride;
        }

        int renderOrder = GetPassTypePriority(item.passType);
        if (item.blendMode != BlendMode::kBlendModeNone) {
            renderOrder += 10000;
        }

        return renderOrder;
    }
}

#include "pch.h"
#include "CoreComponentFactory.h"
#include "../EngineSystem.h"
#include "WinApp/WinApp.h"

#include "Utility/FrameRate/FrameRateController.h"
#include "Input/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"

namespace CoreEngine
{
    void CoreComponentFactory::SetupFrameRate(EngineSystem& engine)
    {
        auto frameRate = std::make_unique<FrameRateController>();
        frameRate->Initialize(); // 60FPS固定
        engine.RegisterComponent(std::move(frameRate));
    }

    void CoreComponentFactory::SetupInput(EngineSystem& engine)
    {
        auto inputManager = std::make_unique<InputManager>();
        inputManager->Initialize(engine.GetWinApp()->GetInstance(), engine.GetWinApp()->GetHwnd());
        engine.RegisterComponent(std::move(inputManager));
    }

    void CoreComponentFactory::SetupAudio(EngineSystem& engine)
    {
        auto audioSystem = std::make_unique<AudioSystem>();

        // 初期化（XAudio2 デバイス開通 + MFStartup）は実測 0.676 秒すべてが待ちで、
        // CPU を 1 ミリ秒も使わない。起動シーケンス上で待つ理由が無いので裏で流す。
        // 実際に音を使う入口（LoadClip / Play / Shutdown）が内部で合流するので、
        // 呼び出し側が完了を管理する必要は無い
        // （SetMasterVolume は待たずに値だけ覚えて初期化完了時に反映する）
        audioSystem->BeginInitializeAsync();

        engine.RegisterComponent(std::move(audioSystem));
    }

    void CoreComponentFactory::SetupLight(EngineSystem& engine)
    {
        auto* dxCommon = engine.GetService<GraphicsCore>();
        auto* resourceFactory = engine.GetService<ResourceFactory>();
        auto* descriptorAllocator = dxCommon->GetDescriptorAllocator();

        auto lightManager = std::make_unique<LightManager>();
        lightManager->Initialize(dxCommon->GetDevice(), resourceFactory, descriptorAllocator);

        // デフォルトライトは作成しない（各シーンで個別に作成する）

        LightManager* lightManagerPtr = lightManager.get();
        engine.RegisterComponent(std::move(lightManager));

        // Model / SkinnedModel の両レンダラーに LightManager を一括設定
        if (auto* renderManager = engine.GetService<RenderManager>()) {
            for (auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
                if (auto* r = dynamic_cast<BaseModelRenderer*>(renderManager->GetRenderer(passType))) {
                    r->SetLightManager(lightManagerPtr);
                }
            }

            // モデルパーティクルは形状由来の法線を持つのでライティングする
            // （板ポリパーティクルは常にカメラを向くため対象外）
            if (auto* r = dynamic_cast<ModelParticleRenderer*>(
                    renderManager->GetRenderer(RenderPassType::ModelParticle))) {
                r->SetLightManager(lightManagerPtr);
            }
        }
    }
}

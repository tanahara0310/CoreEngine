#include "pch.h"
#include "CoreComponentFactory.h"
#include "../EngineSystem.h"
#include "WinApp/WinApp.h"

#include "Utility/FrameRate/FrameRateController.h"
#include "Input/InputManager.h"
#include "Audio/SoundManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"

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
        auto soundManager = std::make_unique<SoundManager>();

        // 初期化（XAudio2 デバイス開通 + MFStartup）は実測 0.676 秒すべてが待ちで、
        // CPU を 1 ミリ秒も使わない。起動シーケンス上で待つ理由が無いので裏で流す。
        // 実際に音を使う入口（LoadSound / PlaySound / SetMasterVolume / Shutdown）が
        // EnsureInitialized() で合流するので、呼び出し側の変更は要らない
        soundManager->BeginInitializeAsync();

        engine.RegisterComponent(std::move(soundManager));
    }

    void CoreComponentFactory::SetupLight(EngineSystem& engine)
    {
        auto* dxCommon = engine.GetService<GraphicsCore>();
        auto* resourceFactory = engine.GetService<ResourceFactory>();
        auto* descriptorManager = dxCommon->GetDescriptorManager();

        auto lightManager = std::make_unique<LightManager>();
        lightManager->Initialize(dxCommon->GetDevice(), resourceFactory, descriptorManager);

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
        }
    }
}

#include "pch.h"
#include "AssignmentScene.h"

#include "Editor/Environment/AtmosphereEditor.h"
#include "Graphics/Model/ModelManager.h"
#include "Input/InputManager.h"
#include "Input/KeyboardInput.h"
#include "Particle/IParticleSystem.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace {
    /// walk.gltf（Mixamo）の右手ジョイント名。武器の追従先
    constexpr const char* kRightHandJointName = "mixamorig:RightHand";
    /// 同じく左手ジョイント名。パーティクルの発生位置
    constexpr const char* kLeftHandJointName = "mixamorig:LeftHand";
}

namespace CoreEngine
{
    void AssignmentScene::OnInitialize()
    {
        SetSceneName("AssignmentScene");

        auto modelManager = engine_->GetService<ModelManager>();
        if (!modelManager) {
            return; // 必須コンポーネントがない場合は何も生成しない
        }

        // ===== 太陽ライト =====
        // 空は BaseScene が既定で大気散乱モードの SkyBox（＋雲）を自動生成する。
        // SetupLight() の既定値（天頂・intensity=1）は大気散乱の輝度スケールと
        // 整合しないため、他の大気シーンと同じ定石で明示的に上書きする。
        if (directionalLight_) {
            directionalLight_->direction = AtmosphereEditor::ComputeSunLightDirection(35.0f, 25.0f);
            // 空（大気・雲）の輝度スケールとサーフェスの直接光は単位系が別なので分離して与える
            directionalLight_->atmosphereIntensity = 20.0f;
            directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
        }

        // ===== スキニングモデル =====
        // walk.gltf は身長 1.66m・足元 y≒0 のモデルなので、
        // BaseScene 既定の無限遠タイル床（y=0）にそのまま接地する。
        // AnimatedModelObject が
        //   アニメーションのロード → スケルトン生成 → SkinCluster 作成
        // まで面倒を見るため、シーン側は配置だけを行う。
        // 描画は RenderPassType::SkinnedModel 経路に入り、頂点変形は
        // SkinningComputeDispatcher（Skinning.CS.hlsl）が ComputeShader で実行する。
        character_ = CreateObject<WalkModelObject>();
        character_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        character_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        character_->GetTransform().rotate = { 0.0f, 0.0f, 0.0f };
        character_->SetActive(true);

        // 骨のデバッグ表示（B キーで切り替え可能）。
        // スケルトンの親子関係を緑の線、ジョイント位置を黄色の十字で描画する。
        character_->SetSkeletonDebugDrawEnabled(true);

        // ===== 武器を右手に持たせる（ジョイントソケットアタッチ） =====
        // 追従は SkeletonSocketComponent が LateUpdate で行う。GameObjectManager は
        // 全オブジェクトの Update が終わってから LateUpdate をまとめて回すので、
        // **生成順は問わない**（以前は「キャラクターより後に生成すること」という
        // 制約があり、守らないと 1 フレーム前の姿勢を参照していた）。
        weapon_ = CreateObject<WeaponObject>();
        weapon_->AttachToJoint(character_, kRightHandJointName);
        // 刃は自分のローカル Y 軸方向に伸びる。手ジョイントの +Y は指先側を向いているので、
        // 刃の長さの半分だけ +Y へずらすと「柄が手の中、刃が指先の向こう」になる。
        // 見え方を変えたい場合はここの平行移動・回転を調整する（-Y にすると担ぐ向きになる）。
        weapon_->SetSocketOffset({ 0.0f, 0.32f, 0.0f }, { 0.0f, 0.0f, 0.0f });
        if (auto* mat = weapon_->GetModel()->GetMaterial()) {
            mat->SetColor({ 0.75f, 0.78f, 0.85f, 1.0f });
            mat->SetMetallic(1.0f);
            mat->SetRoughness(0.25f);
            mat->SetLightingEnabled(true);
        }
        weapon_->SetActive(true);

        // ===== 左手から GPU パーティクルを出す =====
        // ComputeShader で更新する GpuParticleSystem を使い、
        // エミッター位置を左手ジョイントに毎フレーム追従させる。
        handParticles_ = CreateParticleSystem(ParticleBackend::GPU, "HandParticles");
        if (handParticles_) {
            handParticles_->SetTexture("circle.png");
            handParticles_->SetBlendMode(BlendMode::kBlendModeAdd);
            handParticles_->SetBillboardType(BillboardType::ViewFacing);

            auto& main = handParticles_->GetMainModule().GetMainData();
            main.looping = true;
            main.playOnAwake = true;
            main.maxParticles = 4096;
            main.simulationSpace = MainModule::SimulationSpace::World; // 手が動いても既存の粒はその場に残る
            main.startLifetime = 0.9f;
            main.startLifetimeRandomness = 0.3f;
            main.startSpeed = 0.6f;
            main.startSpeedRandomness = 0.5f;
            main.startSize = { 0.045f, 0.045f, 0.045f };
            main.startSizeRandomness = 0.4f;
            // 加算合成なので粒が重なるほど白飛びする。色味を残すため控えめの輝度にする
            main.startColor = { 1.0f, 0.45f, 0.12f, 0.5f };

            auto& emission = handParticles_->GetEmissionModule().GetEmissionData();
            emission.rateOverTime = 120;
            emission.burstCount = 0;

            // 手のまわりの小さな球から全方向へ放出する
            ShapeModule::ShapeData shape{};
            shape.shapeType = ShapeModule::ShapeType::Sphere;
            shape.radius = 0.04f;
            handParticles_->GetShapeModule().SetShapeData(shape);

            // 炎のようにゆっくり立ち上らせる（重力を打ち消して上向きの力を与える）
            ForceModule::ForceData force{};
            force.gravity = { 0.0f, 0.9f, 0.0f };
            force.drag = 0.4f;
            handParticles_->GetForceModule().SetForceData(force);

            character_->SetHandParticleSystem(handParticles_, kLeftHandJointName);
            handParticles_->Play();
        }

        // ===== マルチメッシュ・マルチマテリアルのスキニングモデル =====
        // BrainStem.gltf は 1 メッシュが 59 プリミティブに分かれ、それぞれ別マテリアルを持つ。
        // ModelData 側は subMeshes / materials が配列になっており、
        // Model::Draw() がサブメッシュごとに materialIndex を引いてマテリアル定数バッファと
        // テクスチャを差し替えながら描画する。
        multiMaterialModel_ = CreateObject<BrainStemObject>();
        multiMaterialModel_->GetTransform().translate = { 2.5f, 0.0f, 0.0f };
        multiMaterialModel_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        multiMaterialModel_->GetTransform().rotate = { 0.0f, 0.0f, 0.0f };
        multiMaterialModel_->SetActive(true);

        if (const Model* model = multiMaterialModel_->GetModel()) {
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::General,
                "AssignmentScene: BrainStem materialCount={}", model->GetMaterialCount());
        }

        // ===== アニメーションブレンド（Fox） =====
        // Fox.gltf は同一スケルトンで動くアニメーションを 3 本（Survey / Walk / Run）持つ。
        // Z / X / C キーでブレンドしながら切り替えられる。
        // 頂点が cm 単位で作られておりルートノードにスケールが無いため、ここで 0.01 倍する。
        fox_ = CreateObject<FoxObject>();
        fox_->GetTransform().translate = { -2.5f, 0.0f, 0.0f };
        fox_->GetTransform().scale = { 0.01f, 0.01f, 0.01f };
        fox_->GetTransform().rotate = { 0.0f, 0.0f, 0.0f };
        fox_->SetActive(true);

        // ===== カメラ =====
        // 3 体（Fox x=-2.5 / キャラ x=0 / BrainStem x=2.5）がまとめて収まる位置から
        // 正面（+Z 方向）を向かせる
        SetReleaseCameraTransform({ 0.0f, 1.0f, -6.5f });

        Logger::GetInstance().Log("AssignmentScene: スキニングモデルを配置しました");
    }

    void AssignmentScene::OnUpdate()
    {
        // KeyboardInput は InputManager が内部で所有しており、エンジンコンポーネントとしては
        // 登録されていない。GetService<KeyboardInput>() は必ず nullptr を返すので、
        // 入力は InputManager のクエリ経由で取ること。
        auto inputManager = engine_->GetService<InputManager>();
        if (!inputManager) {
            return;
        }
        const auto& keyboard = inputManager->GetQuery();

        // Tab キーでシーンをリスタート
        if (keyboard.IsKeyTriggered(DIK_TAB)) {
            if (sceneManager_) {
                sceneManager_->ChangeScene("AssignmentScene");
            }
            return;
        }

        // Z / X / C キーで Fox のアニメーションをブレンドしながら切り替える。
        // ブレンド中に別のクリップを押しても AnimationBlender がターゲットを差し替えるので破綻しない。
        if (fox_) {
            constexpr float kBlendDuration = 0.35f;
            if (keyboard.IsKeyTriggered(DIK_Z)) {
                fox_->SwitchAnimationWithBlend(FoxObject::kClipSurvey, kBlendDuration);
            } else if (keyboard.IsKeyTriggered(DIK_X)) {
                fox_->SwitchAnimationWithBlend(FoxObject::kClipWalk, kBlendDuration);
            } else if (keyboard.IsKeyTriggered(DIK_C)) {
                fox_->SwitchAnimationWithBlend(FoxObject::kClipRun, kBlendDuration);
            }
        }

        // B キーで骨のデバッグ表示をまとめて切り替える
        if (keyboard.IsKeyTriggered(DIK_B)) {
            const bool enabled = character_ && !character_->IsSkeletonDebugDrawEnabled();
            if (character_) {
                character_->SetSkeletonDebugDrawEnabled(enabled);
            }
            if (multiMaterialModel_) {
                multiMaterialModel_->SetSkeletonDebugDrawEnabled(enabled);
            }
        }
    }

    void AssignmentScene::Draw()
    {
        BaseScene::Draw();
    }
}

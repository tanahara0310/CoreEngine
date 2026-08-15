#include "pch.h"
#include "GroundFeature.h"

#include "Camera/Camera.h"
#include "Collision/ColliderComponent.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef USE_IMGUI
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace
{
    using namespace CoreEngine;

#ifdef USE_IMGUI
    /// 設定パネルの編集対象（シーン寿命のポインタをラムダに持たせないための
    /// ファイルスコープ変数。GridRenderer と同じ流儀）
    GroundFeature* s_activeGround = nullptr;

    /// パネルが扱う CVar の接頭辞
    constexpr const char* kGroundCVarPrefix = "r.Ground";
#endif

    // ───────────────────────────────────────────────────────────────
    // 既定床のパラメータ（CVar。Engine Settings と Saved JSON に自動で載る）
    // ───────────────────────────────────────────────────────────────

    CVar<bool> cvEnable{
        "r.Ground.Enable", true,
        "既定の床（どのシーンにも必ずあるベース地面）を描く" };

    CVar<bool> cvUseAtmosphereAlbedo{
        "r.Ground.UseAtmosphereAlbedo", true,
        "床の色を大気散乱の地表アルベドに追従させる（床の縁と地平線の色段差を防ぐ）" };

    CVar<Vector4> cvColor{
        "r.Ground.Color", { 0.3f, 0.3f, 0.3f, 1.0f },
        "床のベースカラー（UseAtmosphereAlbedo が OFF のときに使う）" };

    CVar<float> cvRoughness{
        "r.Ground.Roughness", 0.95f,
        "床のラフネス（既定はほぼ拡散反射）", CVarRange{ 0.0f, 1.0f } };

    CVar<bool> cvChecker{
        "r.Ground.Checker", false,
        "床にチェッカー柄を貼る（スケール把握・移動量の確認用）" };

    CVar<float> cvCheckerTiles{
        "r.Ground.CheckerTiles", 64.0f,
        "床の端から端までのタイル数（大きいほど細かい）", CVarRange{ 1.0f, 512.0f } };

    /// 床メッシュの分割数。
    /// 2 三角形（=1）のまま数 km へ引き伸ばすと、1 枚の三角形が画面全体を覆う。
    /// ラスタライザの深度・属性補間は頂点から離れるほど精度が落ちるため、
    /// 深度に依存する後段（SSAO・RT シャドウ・Aerial Perspective）が
    /// 頂点から最も遠いカメラ足元＝画面下側で暴れやすくなる。
    CVar<int> cvSubdivisions{
        "r.Ground.Subdivisions", 32,
        "床メッシュの分割数（1 で 2 三角形。小さいと足元の深度精度が落ちてちらつく）",
        CVarRange{ 1.0f, 128.0f } };

    CVar<float> cvSizeScale{
        "r.Ground.SizeScale", 1.2f,
        "床の半幅 ÷ カメラのファークリップ。1.0 で正面方向がちょうどファークリップまで",
        CVarRange{ 0.5f, 8.0f } };

    CVar<float> cvMinHalfSize{
        "r.Ground.MinHalfSize", 500.0f,
        "床の半幅の下限 [m]（ファークリップが極端に近いシーン用）",
        CVarRange{ 10.0f, 20000.0f } };

    CVar<float> cvMaxHalfSize{
        "r.Ground.MaxHalfSize", 4000.0f,
        "床の半幅の上限 [m]（ファークリップが極端に遠いカメラで巨大化させない）",
        CVarRange{ 100.0f, 100000.0f } };

    CVar<bool> cvFollowCamera{
        "r.Ground.FollowCamera", true,
        "床をカメラへ追従させる（タイル単位でスナップするので柄は滑らない）" };

    CVar<bool> cvCollision{
        "r.Ground.Collision", true,
        "床に当たり判定を持たせる（落下防止）" };

    CVar<float> cvThickness{
        "r.Ground.Thickness", 20.0f,
        "床コライダーの厚み [m]（薄すぎると高速な物体がすり抜ける）",
        CVarRange{ 0.1f, 500.0f } };

    /// UV タイル数の下限（0 除算とゼロ幅タイルを防ぐ）
    constexpr float kMinUvTiling = 1.0f;

    /// @brief CVar から床メッシュの分割数を取り出す（範囲外を弾く）
    uint32_t ResolveSubdivisions()
    {
        return static_cast<uint32_t>(std::clamp(cvSubdivisions.Get(), 1, 128));
    }

    /// カメラが取得できない場合に使うファークリップ [m]（CameraParameters の既定と同値）
    constexpr float kFallbackFarClip = 1000.0f;

    /// チェッカー用テクスチャと、単色時に使う 1x1 白テクスチャ
    constexpr const char* kCheckerTexture = "uvChecker.png";
    constexpr const char* kWhiteTexture = "white1x1.png";

    /// @brief 大気マネージャを引く（大気を持たない構成では nullptr）
    AtmosphereManager* GetAtmosphere(SceneContext& ctx)
    {
        auto* domain = ctx.engine ? ctx.engine->GetRenderDomainContext() : nullptr;
        return domain ? domain->GetAtmosphereManager() : nullptr;
    }
}

namespace CoreEngine
{
    void GroundFeature::Initialize([[maybe_unused]] SceneContext& ctx)
    {
#ifdef USE_IMGUI
        // パラメータ UI は CVar から自動生成する。全 CVar を一覧する横断パネルは
        // 存在しない設計なので、機能ごとにこの登録をしないとどこにも出てこない
        EnsureSettingsPanelRegistered(ctx.engine);
        SetActiveForSettingsPanel(this);
#endif
    }

    void GroundFeature::PostSceneInitialize(SceneContext& ctx)
    {
        // 抑止されているシーンでは床オブジェクト自体を作らない
        // （後から CVar で有効化することもできないが、独自地形と二重になるより良い）
        if (suppressed_) {
            Logger::GetInstance().Infof(LogCategory::System,
                "GroundFeature: シーン側の指定により既定床を生成しません");
            return;
        }

        CreateGroundObject(ctx);
    }

    void GroundFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        // GameObject の更新前に位置を確定させる（Transform の GPU 転送はこの後に走る）
        if (phase != SceneUpdatePhase::PreObjectUpdate) {
            return;
        }

        // Hierarchy から床を削除された場合は手を引く。実体の解放はフレーム末なので、
        // 削除マークの時点で参照を捨てておけばダングリングポインタにならない
        if (ground_ && ground_->IsMarkedForDestroy()) {
            ground_ = nullptr;
            mesh_ = nullptr;
            material_ = nullptr;
            collider_ = nullptr;
        }
        if (!ground_) {
            return;
        }

        const bool wanted = cvEnable.Get();
        if (ground_->IsActive() != wanted) {
            ground_->SetActive(wanted);
        }
        if (!wanted) {
            return;
        }

        UpdateTransform(ctx);
        SyncMaterial(ctx);
    }

    void GroundFeature::Finalize(SceneContext&)
    {
#ifdef USE_IMGUI
        // シーンと一緒に消えるので、パネルの参照を先に外す
        SetActiveForSettingsPanel(nullptr);
#endif
        // 実体は GameObjectManager が持っているのでポインタを切るだけ
        ground_ = nullptr;
        mesh_ = nullptr;
        material_ = nullptr;
        collider_ = nullptr;
    }

    void GroundFeature::CreateGroundObject(SceneContext& ctx)
    {
        if (!ctx.gameObjectManager) {
            return;
        }

        auto object = std::make_unique<GameObject>();
        object->SetName("DefaultGround");
        ground_ = ctx.gameObjectManager->AddObject(std::move(object));
        if (!ground_) {
            return;
        }

        // エンジンが毎シーン作るオブジェクトなので、シーン JSON の対象から外す
        // （外し忘れると全シーンの保存データに床が増えていく）
        ground_->SetSerializeEnabled(false);

        // 1x1 の単位平面を 1 枚だけ作り、広さは Transform のスケールで変える。
        // UV はメッシュに焼き込むので、スケールを変えてもタイル数は一定に保たれる。
        meshUvTiling_ = (std::max)(cvCheckerTiles.Get(), kMinUvTiling);
        meshSubdivisions_ = ResolveSubdivisions();
        mesh_ = ground_->AddComponent<MeshRendererComponent>(
            std::make_unique<PlaneMeshGenerator>(
                1.0f, 1.0f, meshSubdivisions_, meshSubdivisions_, meshUvTiling_));
        material_ = ground_->AddComponent<MaterialComponent>();

        // Start() を先に走らせてマテリアル参照を確定させる（この直後に色を入れるため）
        ground_->DispatchComponentStart();

        // コライダーの実効サイズ = shape.size × オーナーのワールドスケール。
        // XZ に 1 を渡すことで床の全幅に一致し、Y は scale.y = 1 固定なので
        // shape.size.y がそのままワールドの厚みになる（UpdateTransform で更新）。
        // レイヤーは Default。既定の衝突マトリクスでは Default だけが全レイヤーと
        // 当たるため、Environment にすると Player 等とすり抜ける。
        collider_ = &ground_->GetColliders().AddBox({ 1.0f, 1.0f, 1.0f }, CollisionLayer::Default);
        collider_->SetTrigger(false);  // 通知だけでなく押し出す（＝床の上に立てる）
        collider_->SetStatic(true);    // 床自身は押し返されない（相手を全量押し出す）

        UpdateTransform(ctx);
        SyncMaterial(ctx);

        Logger::GetInstance().Infof(LogCategory::System,
            "GroundFeature: 既定床を生成（半幅 %.0fm）", ComputeHalfSize(ctx.gameViewCamera3D));
    }

    float GroundFeature::ComputeHalfSize(const Camera* camera)
    {
        // 床はカメラのファークリップより先には描かれないので、広さの基準はファークリップ。
        // 「地平線まで床で埋める」ことは far=1000m の既定では原理的に不可能で
        // （高度 3m でも地平線は約 6.2km 先）、床の縁より遠方は大気散乱の地表項が
        // 受け持つ。両者の色を合わせてあるので、そこに段差は出ない。
        // 上限は必須。エディタカメラのファークリップは保存値で 50km まで伸びることがあり、
        // そのまま掛けると 60km 四方の床になって深度精度とレイトレの AS が無駄に太る。
        const float farClip = camera ? camera->GetParameters().farClip : kFallbackFarClip;
        const float minHalf = (std::max)(cvMinHalfSize.Get(), 1.0f);
        const float maxHalf = (std::max)(cvMaxHalfSize.Get(), minHalf);
        const float half = farClip * (std::max)(cvSizeScale.Get(), 0.1f);
        return std::clamp(half, minHalf, maxHalf);
    }

    void GroundFeature::UpdateTransform(SceneContext& ctx)
    {
        auto* transform = ground_->GetComponent<TransformComponent>();
        if (!transform) {
            return;
        }

        currentHalfSize_ = ComputeHalfSize(ctx.gameViewCamera3D);
        const float fullSize = currentHalfSize_ * 2.0f;
        // Y スケールは 1 のまま（コライダーの厚みをワールド量で扱うための不変条件）
        transform->Scale() = { fullSize, 1.0f, fullSize };

        Vector3 center{ 0.0f, ResolveGroundLevelY(ctx), 0.0f };
        if (cvFollowCamera.Get() && ctx.gameViewCamera3D) {
            const Vector3 cameraPosition = ctx.gameViewCamera3D->GetPosition();

            // タイル間隔でスナップする。連続追従にすると柄がカメラへ貼り付いて
            // 動かなくなり、床の上を滑っているように見えるため。
            const float tile = fullSize / (std::max)(meshUvTiling_, kMinUvTiling);

            // ただし毎フレーム最近傍へスナップしてはいけない。タイル境界を跨ぐたびに
            // 床が 1 タイル分ジャンプし、モーションベクタ（prevWVP 由来）が
            // 「床全体が瞬間移動した」と報告する。柄は一致するので画は変わらないのに
            // TAA・RT シャドウのテンポラル蓄積・モーションブラーだけが嘘の速度を見て
            // 履歴を壊す。カメラが床の中心から十分離れたときだけ置き直せば、
            // 移動が続く間もジャンプは半幅の 25% 進むごと（既定 300m 級）に減る。
            const float slack = (std::max)(fullSize * 0.125f, tile);
            const bool needsRecenter =
                !recentered_ ||
                std::abs(cameraPosition.x - groundCenterXZ_.x) > slack ||
                std::abs(cameraPosition.z - groundCenterXZ_.y) > slack;

            if (needsRecenter) {
                groundCenterXZ_.x = std::round(cameraPosition.x / tile) * tile;
                groundCenterXZ_.y = std::round(cameraPosition.z / tile) * tile;
                recentered_ = true;
            }
            center.x = groundCenterXZ_.x;
            center.z = groundCenterXZ_.y;
        }
        transform->Translate() = center;

        if (collider_) {
            const float thickness = (std::max)(cvThickness.Get(), 0.01f);
            collider_->SetSize({ 1.0f, thickness, 1.0f });
            // 箱の上面を床面に合わせる（中心を厚みの半分だけ下げる）。
            // オフセットにもワールドスケールが乗るが、Y スケールは 1 固定なので素の値でよい。
            collider_->SetOffset({ 0.0f, -thickness * 0.5f, 0.0f });
            collider_->SetEnabled(cvCollision.Get());
        }
    }

    void GroundFeature::SyncMaterial(SceneContext& ctx)
    {
        // タイル数・分割数を変えたらメッシュごと作り直す（どちらもメッシュに焼き込んであるため）。
        // 生成結果は ModelManager がキャッシュキー単位で持つので、値を往復しても
        // 作り直しは 1 回ずつで済む。
        const float wantedTiling = (std::max)(cvCheckerTiles.Get(), kMinUvTiling);
        const uint32_t wantedSubdivisions = ResolveSubdivisions();
        if (mesh_ && (wantedTiling != meshUvTiling_ || wantedSubdivisions != meshSubdivisions_)) {
            meshUvTiling_ = wantedTiling;
            meshSubdivisions_ = wantedSubdivisions;
            mesh_->SetPrimitive(
                std::make_unique<PlaneMeshGenerator>(
                    1.0f, 1.0f, meshSubdivisions_, meshSubdivisions_, meshUvTiling_));
            mesh_->ReloadFromSpec();
            appliedChecker_ = -1;  // 新しいモデルへテクスチャを入れ直す
        }

        if (!material_) {
            return;
        }

        // 色は既定で大気の地表アルベドに追従する。床の縁より遠方は Sky-View LUT の
        // 地表項が描くので、ここを別値にすると床の縁が色の境界として見えてしまう。
        Vector4 color = cvColor.Get();
        if (cvUseAtmosphereAlbedo.Get()) {
            if (const AtmosphereManager* atmosphere = GetAtmosphere(ctx)) {
                const Vector3& albedo = atmosphere->GetParameters().groundAlbedo;
                color = { albedo.x, albedo.y, albedo.z, 1.0f };
            }
        }
        material_->SetColor(color);
        material_->SetPBR(0.0f, std::clamp(cvRoughness.Get(), 0.0f, 1.0f), 1.0f);

        // テクスチャの差し替えだけはロードを伴うので、切り替わった瞬間にだけ行う
        const int checker = cvChecker.Get() ? 1 : 0;
        if (mesh_ && checker != appliedChecker_) {
            mesh_->SetTexture(checker ? kCheckerTexture : kWhiteTexture);
            appliedChecker_ = checker;
        }
    }

    float GroundFeature::ResolveGroundLevelY(SceneContext& ctx)
    {
        if (const AtmosphereManager* atmosphere = GetAtmosphere(ctx)) {
            return atmosphere->GetParameters().groundLevelY;
        }
        return 0.0f;
    }

#ifdef USE_IMGUI

    void GroundFeature::EnsureSettingsPanelRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        auto* debug = engine->GetDebugSubsystem();
        auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
        if (!gameDebugUI) {
            return;
        }

        // ドロワーは何もキャプチャしない（ファイルスコープの s_activeGround を読むだけ）
        gameDebugUI->RegisterEnginePanel("Ground", [] {
            if (s_activeGround) {
                s_activeGround->DrawSettingsImGui();
            } else {
                ImGui::TextDisabled("(シーンがありません)");
            }
        });

        registered = true;
    }

    void GroundFeature::SetActiveForSettingsPanel(GroundFeature* ground)
    {
        s_activeGround = ground;
    }

    void GroundFeature::DrawSettingsImGui()
    {
        // 現在の状態。「CVar は ON なのに床が無い」がシーン側の抑止によるものだと分かるようにする
        if (suppressed_) {
            ImGui::TextDisabled("このシーンは既定床を抑止しています");
            ImGui::TextDisabled("（SetDefaultGroundEnabled(false) を呼んでいます）");
        } else if (ground_) {
            ImGui::Text("半幅 %.0f m（カメラのファークリップ基準）", currentHalfSize_);
        } else {
            ImGui::TextDisabled("床は生成されていません");
        }
        ImGui::Spacing();

        // パラメータ UI は CVar から自動生成される（値は Update が毎フレーム取り込む）
        CVarUI::DrawTree(kGroundCVarPrefix);

        ImGui::Spacing();
        if (ImGui::Button("パラメータを既定値にリセット")) {
            CVarUI::ResetTree(kGroundCVarPrefix);
        }
    }

#endif // USE_IMGUI
}

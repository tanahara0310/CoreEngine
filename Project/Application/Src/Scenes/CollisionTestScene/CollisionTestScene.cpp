#include "pch.h"
#include "CollisionTestScene.h"

#include "CollisionTestReport.h"
#include "GeometrySelfTest.h"

#include "Collision/CollisionWorld.h"
#include "Editor/Environment/AtmosphereEditor.h"
#include "Input/KeyboardInput.h"
#include "Scene/SceneManager.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace CoreEngine;

namespace CollisionTest
{
    namespace {
        /// @brief 衝突カウンタを表示用文字列へ
        std::string StatsStr(const ProbeStats& stats)
        {
            return "Enter=" + std::to_string(stats.enter)
                 + " Stay=" + std::to_string(stats.stay)
                 + " Exit=" + std::to_string(stats.exit);
        }

        /// @brief 行の Z 座標を note 用文字列へ
        std::string RowNote(float z, const char* extra = "")
        {
            std::string note = "シーン上の z = " + std::to_string(static_cast<int>(z)) + " の列。";
            if (extra && *extra) {
                note += "\n";
                note += extra;
            }
            return note;
        }

        const Vector4 kColorNeutral { 0.72f, 0.75f, 0.80f, 1.0f };
        const Vector4 kColorTarget  { 0.30f, 0.55f, 0.85f, 1.0f };
        const Vector4 kColorMover   { 0.85f, 0.80f, 0.35f, 1.0f };
        const Vector4 kColorVictim  { 0.55f, 0.35f, 0.75f, 1.0f };
    }

    // ─────────────────────────────────────────────────────────────────────
    // 初期化
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::OnInitialize()
    {
        SetSceneName("CollisionTestScene");

        Report::Get().BeginSession("CollisionTestScene");
#ifdef USE_IMGUI
        Report::EnsureRegistered(engine_);
#endif

        // 純関数（Math/Geometry）の自己テストは 1 回だけ
        RunGeometrySelfTest();

        // ===== 太陽ライト（他の大気シーンと同じ定石） =====
        if (directionalLight_) {
            directionalLight_->direction = AtmosphereEditor::ComputeSunLightDirection(40.0f, 30.0f);
            directionalLight_->atmosphereIntensity = 20.0f;
            directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
        }

        // 全ての行を俯瞰できる位置へ
        SetReleaseCameraTransform({ 0.0f, 34.0f, -26.0f }, { 0.62f, 0.0f, 0.0f });

        // ===== レイヤーマトリクス =====
        // T4 のために Player×Item は「有効化しない」。
        // T5 のために Default×Default も「有効化しない」（既定状態を検証する）。
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy, true);
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Environment, true);
        // T9 の HeadlessProbe 専用ペア（他の行と混ざらないよう独立レイヤーを使う）
        SetCollisionEnabled(CollisionLayer::Boss, CollisionLayer::BossBullet, true);

        // ===== T1: 球 × 球（通過） =====
        t1Static_ = MakeSphereProbe("T1_Target", { 0.0f, kProbeY, RowZ(0) },
            1.0f, CollisionLayer::Enemy, kColorTarget);
        t1Mover_ = MakeSphereProbe("T1_Mover", { kTraverseMinX, kProbeY, RowZ(0) },
            1.0f, CollisionLayer::Player, kColorMover);

        // ===== T2: 球 → 箱（球を先に登録して球側の CheckCollision を通す） =====
        t2Mover_ = MakeSphereProbe("T2_Mover(Sphere)", { kTraverseMinX, kProbeY, RowZ(1) },
            1.0f, CollisionLayer::Player, kColorMover);
        t2Box_ = MakeBoxProbe("T2_Target(Box)", { 0.0f, kProbeY, RowZ(1) },
            2.0f, CollisionLayer::Environment, kColorTarget);

        // ===== T3: 箱 → 球（箱を先に登録して箱側の CheckCollision を通す） =====
        t3Box_ = MakeBoxProbe("T3_Target(Box)", { 0.0f, kProbeY, RowZ(2) },
            2.0f, CollisionLayer::Environment, kColorTarget);
        t3Mover_ = MakeSphereProbe("T3_Mover(Sphere)", { kTraverseMinX, kProbeY, RowZ(2) },
            1.0f, CollisionLayer::Player, kColorMover);

        // ===== T4: レイヤーフィルタ（Player×Item は無効のまま） =====
        t4Item_ = MakeSphereProbe("T4_Target(Item)", { 0.0f, kProbeY, RowZ(3) },
            1.0f, CollisionLayer::Item, kColorNeutral);
        t4Mover_ = MakeSphereProbe("T4_Mover(Player)", { kTraverseMinX, kProbeY, RowZ(3) },
            1.0f, CollisionLayer::Player, kColorMover);

        // ===== T5: Default × Default（常時重なり） =====
        t5A_ = MakeSphereProbe("T5_DefaultA", { -0.6f, kProbeY, RowZ(4) },
            1.0f, CollisionLayer::Default, kColorNeutral);
        t5B_ = MakeSphereProbe("T5_DefaultB", { 0.6f, kProbeY, RowZ(4) },
            1.0f, CollisionLayer::Default, kColorNeutral);

        // ===== T6: 重なったまま片方を破棄 =====
        t6Survivor_ = MakeSphereProbe("T6_Survivor", { -0.5f, kProbeY, RowZ(5) },
            1.0f, CollisionLayer::Player, kColorTarget);
        t6Victim_ = MakeSphereProbe("T6_Victim", { 0.5f, kProbeY, RowZ(5) },
            1.0f, CollisionLayer::Enemy, kColorVictim);

        // ===== T7: スポーン / 破棄の繰り返し（アドレス再利用） =====
        t7Static_ = MakeSphereProbe("T7_Static", { -0.5f, kProbeY, RowZ(6) },
            1.0f, CollisionLayer::Player, kColorTarget);

        // ===== T8: スケール 2 倍（見た目は接触・コライダーは未反映） =====
        t8A_ = MakeSphereProbe("T8_ScaledA", { -1.5f, kProbeY, RowZ(7) },
            1.0f, CollisionLayer::Player, kColorTarget);
        t8B_ = MakeSphereProbe("T8_ScaledB", { 1.5f, kProbeY, RowZ(7) },
            1.0f, CollisionLayer::Enemy, kColorVictim);
        t8A_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };
        t8B_->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

        // ===== T9: GetWorldPosition を持たないオブジェクト =====
        t9Far_ = CreateObject<HeadlessProbe>();
        t9Far_->SetLabel("T9_Far");
        t9Far_->SetSerializeEnabled(false);
        t9Far_->SetLogicalPosition({ 0.0f, kProbeY, 100.0f });
        t9Far_->AddSphereCollider(1.0f, CollisionLayer::Boss);

        t9Near_ = CreateObject<HeadlessProbe>();
        t9Near_->SetLabel("T9_Near");
        t9Near_->SetSerializeEnabled(false);
        t9Near_->SetLogicalPosition({ 0.0f, kProbeY, -100.0f });
        t9Near_->AddSphereCollider(1.0f, CollisionLayer::BossBullet);

        // ===== T10: コールバック中の RemoveCollider（オプトイン） =====
        t10Static_ = MakeSphereProbe("T10_Static", { -0.5f, kProbeY, RowZ(8) },
            1.0f, CollisionLayer::Player, kColorTarget);
        t10Remover_ = MakeSphereProbe("T10_Remover", { 0.5f, kProbeY, RowZ(8) },
            1.0f, CollisionLayer::Enemy, kColorVictim);
        t10Remover_->SetRemoveColliderOnEnter(true);

        // ===== T11: 1 オブジェクトに本体判定 + 攻撃判定を別レイヤーで 2 本 =====
        // ボスのように「本体に当たったか」と「攻撃判定に当たったか」を区別する構成。
        SetCollisionEnabled(CollisionLayer::Boss, CollisionLayer::Player, true);
        SetCollisionEnabled(CollisionLayer::BossAttack, CollisionLayer::Player, true);

        t11Body_ = MakeSphereProbe("T11_Boss", { 0.0f, kProbeY, RowZ(9) },
            1.0f, CollisionLayer::Boss, kColorTarget);
        // 2 本目: 右へ 2.5 ずらした攻撃判定（オフセット付き）
        t11Body_->GetColliders().AddSphere(1.0f, CollisionLayer::BossAttack,
            { 2.5f, 0.0f, 0.0f });

        // 本体だけに触れる位置と、攻撃判定だけに触れる位置に相手を置く
        t11BodyHit_ = MakeSphereProbe("T11_HitsBody", { -1.2f, kProbeY, RowZ(9) },
            1.0f, CollisionLayer::Player, kColorMover);
        t11AtkHit_ = MakeSphereProbe("T11_HitsAttack", { 3.7f, kProbeY, RowZ(9) },
            1.0f, CollisionLayer::Player, kColorVictim);

        // ===== T12: 壁にぶつかって止まる（押し出し） =====
        // ここだけトリガーを外す。他の行は通知専用のままなので押し出しは起きない。
        t12Wall_ = MakeBoxProbe("T12_Wall", { 0.0f, kProbeY, RowZ(10) },
            2.0f, CollisionLayer::Environment, kColorTarget);
        if (auto* wallCollider = t12Wall_->GetCollider()) {
            wallCollider->SetTrigger(false);
            wallCollider->SetStatic(true);   // 壁は押し返されない
        }

        t12Pusher_ = MakeSphereProbe("T12_Pusher", { kPushStartX, kProbeY, RowZ(10) },
            1.0f, CollisionLayer::Player, kColorMover);
        if (auto* pusherCollider = t12Pusher_->GetCollider()) {
            pusherCollider->SetTrigger(false);
        }

        // ===== T14: レイキャスト問い合わせ =====
        // レイの進行方向に 2 つ並べ、手前だけがヒットすることを確認する。
        // Item レイヤーはどのレイヤーとも衝突しない設定なので、判定ループには
        // 一切現れない = 問い合わせ API が判定とは独立に動くことの確認にもなる。
        t14Near_ = MakeSphereProbe("T14_Near", { -2.0f, kProbeY, RowZ(11) },
            1.0f, CollisionLayer::Item, kColorTarget);
        t14Far_ = MakeSphereProbe("T14_Far", { 3.0f, kProbeY, RowZ(11) },
            1.0f, CollisionLayer::Item, kColorNeutral);

        LogEvent("シーン初期化完了（Tab キーまたはパネルのボタンでリスタート）");
    }

    // ─────────────────────────────────────────────────────────────────────
    // 生成ヘルパー
    // ─────────────────────────────────────────────────────────────────────
    SphereProbe* CollisionTestScene::MakeSphereProbe(const std::string& label, const Vector3& position,
        float radius, CollisionLayer layer, const Vector4& baseColor)
    {
        auto* probe = CreateObject<SphereProbe>(radius, 24u, 12u);
        probe->SetLabel(label);
        probe->SetSerializeEnabled(false);   // シーン JSON でテスト条件が変わらないようにする
        probe->GetTransform().translate = position;
        probe->SetBaseColor(baseColor);
        probe->AddSphereCollider(radius, layer);
        probe->SetActive(true);
        return probe;
    }

    BoxProbe* CollisionTestScene::MakeBoxProbe(const std::string& label, const Vector3& position,
        float size, CollisionLayer layer, const Vector4& baseColor)
    {
        auto* probe = CreateObject<BoxProbe>(size);
        probe->SetLabel(label);
        probe->SetSerializeEnabled(false);
        probe->GetTransform().translate = position;
        probe->SetBaseColor(baseColor);
        probe->AddAABBCollider({ size, size, size }, layer);
        probe->SetActive(true);
        return probe;
    }

    float CollisionTestScene::TraverseX() const
    {
        if (frame_ <= kTraverseStart) { return kTraverseMinX; }
        if (frame_ >= kTraverseEnd)   { return kTraverseMaxX; }

        const float t = static_cast<float>(frame_ - kTraverseStart)
                      / static_cast<float>(kTraverseFrames);
        return kTraverseMinX + (kTraverseMaxX - kTraverseMinX) * t;
    }

    // ─────────────────────────────────────────────────────────────────────
    // 更新（GameObject 更新前 = 位置の確定）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::OnUpdate()
    {
        ++frame_;
        Report::Get().SetFrame(frame_);

        // リスタート要求（パネルのボタン / Tab キー）
        bool restart = Report::Get().ConsumeRestartRequest();
        if (auto* keyboard = engine_->GetService<KeyboardInput>()) {
            if (keyboard->IsKeyTriggered(DIK_TAB)) { restart = true; }
        }
        if (restart && sceneManager_) {
            sceneManager_->ChangeScene("CollisionTestScene");
            return;
        }

        // ===== 通過テストのムーバー位置（毎フレーム再指定＝JSON 復元に影響されない） =====
        const float x = TraverseX();
        if (t1Mover_) { t1Mover_->GetTransform().translate = { x, kProbeY, RowZ(0) }; }
        if (t2Mover_) { t2Mover_->GetTransform().translate = { x, kProbeY, RowZ(1) }; }
        if (t3Mover_) { t3Mover_->GetTransform().translate = { x, kProbeY, RowZ(2) }; }
        if (t4Mover_) { t4Mover_->GetTransform().translate = { x, kProbeY, RowZ(3) }; }

        // ===== 静止プローブの位置も固定し続ける =====
        if (t1Static_)   { t1Static_->GetTransform().translate   = { 0.0f, kProbeY, RowZ(0) }; }
        if (t2Box_)      { t2Box_->GetTransform().translate      = { 0.0f, kProbeY, RowZ(1) }; }
        if (t3Box_)      { t3Box_->GetTransform().translate      = { 0.0f, kProbeY, RowZ(2) }; }
        if (t4Item_)     { t4Item_->GetTransform().translate     = { 0.0f, kProbeY, RowZ(3) }; }
        if (t5A_)        { t5A_->GetTransform().translate        = { -0.6f, kProbeY, RowZ(4) }; }
        if (t5B_)        { t5B_->GetTransform().translate        = { 0.6f, kProbeY, RowZ(4) }; }
        if (t6Survivor_) { t6Survivor_->GetTransform().translate = { -0.5f, kProbeY, RowZ(5) }; }
        if (t7Static_)   { t7Static_->GetTransform().translate   = { -0.5f, kProbeY, RowZ(6) }; }
        if (t8A_)        { t8A_->GetTransform().translate        = { -1.5f, kProbeY, RowZ(7) }; }
        if (t8B_)        { t8B_->GetTransform().translate        = { 1.5f, kProbeY, RowZ(7) }; }
        if (t10Static_)  { t10Static_->GetTransform().translate  = { -0.5f, kProbeY, RowZ(8) }; }
        if (t10Remover_) { t10Remover_->GetTransform().translate = { 0.5f, kProbeY, RowZ(8) }; }
        if (t11Body_)    { t11Body_->GetTransform().translate    = { 0.0f, kProbeY, RowZ(9) }; }
        if (t11BodyHit_) { t11BodyHit_->GetTransform().translate = { -1.2f, kProbeY, RowZ(9) }; }
        if (t11AtkHit_)  { t11AtkHit_->GetTransform().translate  = { 3.7f, kProbeY, RowZ(9) }; }
        if (t12Wall_)    { t12Wall_->GetTransform().translate    = { 0.0f, kProbeY, RowZ(10) }; }
        if (t14Near_)    { t14Near_->GetTransform().translate    = { -2.0f, kProbeY, RowZ(11) }; }
        if (t14Far_)     { t14Far_->GetTransform().translate     = { 3.0f, kProbeY, RowZ(11) }; }

        // T12 のプッシャーだけは位置を「代入」せず「加算」する。
        // 毎フレーム代入すると押し出された結果を上書きしてしまい、テストにならない。
        if (t12Pusher_ && frame_ > kTraverseStart) {
            t12Pusher_->GetTransform().translate.x += kPushSpeed;
        }

        // ===== T6: 重なったまま victim を破棄 =====
        if (frame_ == kDestroyFrame && t6Victim_) {
            LogEvent("T6: 重なったまま T6_Victim を Destroy()");
            t6Victim_->Destroy();
            t6Victim_ = nullptr;   // 破棄後は触らない
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 後処理（PostObjectUpdate の後 = 今フレームの判定結果が出ている）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::OnLateUpdate()
    {
        EvaluateTraverseCases();
        EvaluateStaticCases();
        EvaluateDestroyCase();
        UpdateAbaCase();
        PublishOptInCase();
        EvaluateMultiColliderCase();
        EvaluatePushOutCase();
        EvaluateContactInfoCase();
        EvaluateRaycastQueryCase();

        // 全ケースの結論が出そろうフレームで集計をログへ（GUI を開かずに確認できるようにする）
        const int summaryFrame = (kAbaJudge > kTraverseEnd + 6 ? kAbaJudge : kTraverseEnd + 6) + 5;
        if (frame_ == summaryFrame) {
            int pass = 0, fail = 0, pending = 0;
            for (const auto& c : Report::Get().GetCases()) {
                if (c.status == Status::Pass)      { ++pass; }
                else if (c.status == Status::Fail) { ++fail; }
                else                              { ++pending; }
            }
            LogEvent("===== 集計: PASS " + std::to_string(pass)
                + " / FAIL " + std::to_string(fail)
                + " / 進行中 " + std::to_string(pending) + " =====");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // T1〜T4: 通過テスト
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateTraverseCases()
    {
        const int judgeFrame = kTraverseEnd + 6;
        const bool decided = (frame_ >= judgeFrame);

        // Enter / Exit がちょうど 1 回で、Stay が 1 回以上あることを期待する
        auto publishPassThrough = [&](const char* id, const char* title,
            const ProbeStats& stats, float z, const char* note) {
                CaseResult result;
                result.id       = id;
                result.title    = title;
                result.expected = "Enter=1 Stay>=1 Exit=1";
                result.actual   = StatsStr(stats);
                result.note     = RowNote(z, note);
                result.status   = !decided ? Status::Pending
                    : (stats.enter == 1 && stats.exit == 1 && stats.stay >= 1)
                        ? Status::Pass : Status::Fail;
                Report::Get().Upsert(result);
            };

        if (t1Static_) {
            publishPassThrough("T1", "球×球: 通過で Enter/Stay/Exit",
                t1Static_->Stats(), RowZ(0),
                "黄色の球が青い球を通り抜ける。接触中は赤くなる。");
        }
        if (t2Box_) {
            publishPassThrough("T2", "球→箱: 球側の CheckCollision 経路",
                t2Box_->Stats(), RowZ(1),
                "球を先に生成＝登録順が先なので SphereCollider::CheckCollision(AABB) が走る。");
        }
        if (t3Box_) {
            publishPassThrough("T3", "箱→球: 箱側の CheckCollision 経路",
                t3Box_->Stats(), RowZ(2),
                "箱を先に生成＝AABBCollider::CheckCollision(Sphere) が走る。T2 と同じ答えに\n"
                "なるべき（球×箱の判定が 2 箇所に重複実装されているため、片方だけ壊れても\n"
                "気づけるようにしてある）。");
        }

        if (t4Item_) {
            CaseResult result;
            result.id       = "T4";
            result.title    = "レイヤーフィルタ: Player×Item は無効";
            result.expected = "Enter=0 Stay=0 Exit=0";
            result.actual   = StatsStr(t4Item_->Stats());
            result.note     = RowNote(RowZ(3),
                "マトリクスで有効化していないため、重なってもイベントが飛ばないこと。");
            const auto& s = t4Item_->Stats();
            result.status = !decided ? Status::Pending
                : (s.enter == 0 && s.stay == 0 && s.exit == 0) ? Status::Pass : Status::Fail;
            Report::Get().Upsert(result);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // T5 / T8 / T9: 常時重なり系
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateStaticCases()
    {
        const bool decided = (frame_ >= kStaticJudge);

        // T5: Default × Default
        if (t5A_) {
            CaseResult result;
            result.id       = "T5";
            result.title    = "Default×Default: 既定レイヤー同士が当たる";
            result.expected = "Enter>=1";
            result.actual   = StatsStr(t5A_->Stats());
            result.knownBug = "A-4";
            result.note     = RowNote(RowZ(4),
                "レイヤー未指定（既定引数）のオブジェクト同士。SetCollisionEnabled を呼ばず\n"
                "既定のマトリクスだけで当たること。以前は matrix_[Default][Default] だけ\n"
                "false のままでイベントが飛ばなかった。");
            result.status = !decided ? Status::Pending
                : (t5A_->Stats().enter >= 1) ? Status::Pass : Status::Fail;
            Report::Get().Upsert(result);
        }

        // T8: スケール反映
        if (t8A_) {
            CaseResult result;
            result.id       = "T8";
            result.title    = "スケール 2 倍がコライダーに反映される";
            result.expected = "Enter>=1";
            result.actual   = StatsStr(t8A_->Stats());
            result.knownBug = "A-5";
            result.note     = RowNote(RowZ(7),
                "半径 1 の球を scale=2 で置き、中心間 3.0（見た目は明らかに接触）。\n"
                "コライダーが GetWorldScale() を掛けて実効半径 2 で判定すること。\n"
                "以前は radius_ をそのまま使っていたため無反応だった。");
            result.status = !decided ? Status::Pending
                : (t8A_->Stats().enter >= 1) ? Status::Pass : Status::Fail;
            Report::Get().Upsert(result);
        }

        // T9: ModelGameObject 以外の派生でも位置が判定に効くか
        if (t9Far_) {
            CaseResult result;
            result.id       = "T9";
            result.title    = "ModelGameObject 以外の派生でも位置が判定に効く";
            result.expected = "Enter=0（z=+100 と z=-100 は離れている）";
            result.actual   = StatsStr(t9Far_->Stats());
            result.note     =
                "GameObject を直接継承したオブジェクト。GetWorldPosition() は純粋仮想なので\n"
                "実装を強制される（A-3 の「オーバーライド忘れで全員が原点」はコンパイル\n"
                "エラーになり、実行時には再現しなくなった）。ここでは実装済みの位置が\n"
                "正しく判定に使われることを確認する。";
            result.status = !decided ? Status::Pending
                : (t9Far_->Stats().enter == 0) ? Status::Pass : Status::Fail;
            Report::Get().Upsert(result);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // T6: 重なったまま破棄したときの Exit
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateDestroyCase()
    {
        if (!t6Survivor_) { return; }

        const bool decided = (frame_ >= kDestroyJudge);

        CaseResult result;
        result.id       = "T6";
        result.title    = "接触中の相手が破棄されたら Exit が飛ぶ";
        result.expected = "Enter=1 Exit=1";
        result.actual   = StatsStr(t6Survivor_->Stats());
        result.knownBug = "A-1";
        result.note     = RowNote(RowZ(5),
            "重なった状態で相手を Destroy()。登録から外れたコライダーへ CheckAllCollisions が\n"
            "Exit を配ること。以前は previousCollisions_ に生ポインタのキーが残るだけで\n"
            "Exit を発火する経路がなく、生き残った側が接触状態のまま固まっていた。");
        const auto& s = t6Survivor_->Stats();
        result.status = !decided ? Status::Pending
            : (s.enter == 1 && s.exit == 1) ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T7: スポーン / 破棄の繰り返し（アドレス再利用で Enter が Stay になる）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::UpdateAbaCase()
    {
        if (!t7Static_) { return; }

        if (frame_ >= kAbaStart) {
            const int cycleFrame = (frame_ - kAbaStart) % kAbaCycleFrames;
            const int cycleIndex = (frame_ - kAbaStart) / kAbaCycleFrames;

            // 破棄を先に判定する。スポーン側と同じガードに入れると最後のサイクルの
            // 破棄が丸ごとスキップされる（実際にそれで誤 FAIL を出した）。
            if (t7Spawned_ && cycleFrame == kAbaCycleFrames / 2) {
                t7Spawned_->Destroy();
                t7Spawned_ = nullptr;
            }
            else if (cycleFrame == 0 && cycleIndex == t7SpawnCount_ && t7SpawnCount_ < kAbaCycles) {
                // 生成：静止プローブに重なる位置へ
                t7Spawned_ = MakeSphereProbe(
                    "T7_Spawned_" + std::to_string(t7SpawnCount_),
                    { 0.5f, kProbeY, RowZ(6) }, 1.0f, CollisionLayer::Enemy, kColorVictim);
                ++t7SpawnCount_;
            }
        }

        const bool decided = (frame_ >= kAbaJudge);

        // 静止プローブ側で「スポーン回数ぶんの Enter と Exit がそろっているか」を見る。
        // 取りこぼし（Exit 欠落）とアドレス再利用（Enter が Stay に化ける）の両方が
        // カウントのズレとして現れる。
        const auto& stats = t7Static_->Stats();

        CaseResult result;
        result.id       = "T7";
        result.title    = "生成/破棄を繰り返しても毎回 Enter/Exit が飛ぶ";
        result.expected = "Enter=" + std::to_string(kAbaCycles)
                        + " Exit=" + std::to_string(kAbaCycles);
        result.actual   = StatsStr(stats)
                        + " / スポーン " + std::to_string(t7SpawnCount_) + " 回";
        result.knownBug = "A-1";
        result.note     = RowNote(RowZ(6),
            "弾プールと同じパターン。破棄時に Exit を配って履歴から消すことで、解放された\n"
            "コライダーと同じアドレスを新しいコライダーが取っても古いキーと一致しなくなる。\n"
            "以前は Enter が Stay に化けうる状態だった（ABA）。アロケータ任せなので毎回\n"
            "起きるとは限らず、実測では再現しなかった＝この行は潜在バグの見張り役。");
        result.status = !decided ? Status::Pending
            : (stats.enter == kAbaCycles && stats.exit == kAbaCycles) ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T10: コールバック中の RemoveCollider（オプトイン）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::PublishOptInCase()
    {
        const bool optedIn = Report::Get().IsRemoveColliderInCallbackEnabled();

        CaseResult result;
        result.id       = "T10";
        result.title    = "コールバック中の RemoveCollider() が安全";
        result.expected = "クラッシュしない";
        result.actual   = optedIn ? "有効化中（クラッシュしなければ通過）" : "未実施（パネルでオプトイン）";
        result.knownBug = "A-2";
        result.note     = RowNote(RowZ(8),
            "OnCollisionEnter 内で RemoveCollider() を呼ぶと unique_ptr が即時解放され、\n"
            "判定ループが保持している生ポインタが宙に浮く。GameObject::Destroy() は遅延なのに\n"
            "コライダー着脱だけ即時という非対称が原因。");
        // クラッシュしないことの確認なので自動判定はしない（落ちなければ通過、という性質）
        result.status = Status::Pending;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T11: 1 オブジェクトに本体判定 + 攻撃判定（別レイヤー・オフセット付き）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateMultiColliderCase()
    {
        if (!t11Body_ || !t11BodyHit_ || !t11AtkHit_) { return; }

        const bool decided = (frame_ >= kStaticJudge);

        const size_t colliderCount = t11Body_->GetColliders().Count();
        const auto&  bodyHit = t11BodyHit_->Stats();
        const auto&  atkHit  = t11AtkHit_->Stats();

        CaseResult result;
        result.id       = "T11";
        result.title    = "1 オブジェクトに本体判定 + 攻撃判定を持てる";
        result.expected = "コライダー2本・両方の相手が Enter=1";
        result.actual   = "本数=" + std::to_string(colliderCount)
                        + " / 本体側 " + StatsStr(bodyHit)
                        + " / 攻撃側 " + StatsStr(atkHit);
        result.knownBug = "B-3";
        result.note     = RowNote(RowZ(9),
            "Boss レイヤーの本体（中心）と BossAttack レイヤーの攻撃判定（+X に 2.5 オフセット）を\n"
            "1 つのオブジェクトに持たせ、それぞれに触れる位置へ別々の相手を置いている。\n"
            "以前は unique_ptr<Collider> 1 本きりで、レイヤーに Boss と BossAttack が\n"
            "両方あるのに片方しか使えなかった。");
        result.status = !decided ? Status::Pending
            : (colliderCount == 2 && bodyHit.enter == 1 && atkHit.enter == 1)
                ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T12: 壁にぶつかって止まる（めり込み解消）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluatePushOutCase()
    {
        if (!t12Pusher_ || !t12Wall_) { return; }

        const bool decided = (frame_ >= kPushJudge);
        const float x = t12Pusher_->GetTransform().translate.x;

        char actual[96];
        std::snprintf(actual, sizeof(actual), "x = %.3f（期待 %.3f）", x, kPushExpectedX);

        CaseResult result;
        result.id       = "T12";
        result.title    = "非トリガー同士は押し出されて止まる";
        result.expected = "壁面の手前で停止";
        result.actual   = actual;
        result.knownBug = "B-4";
        result.note     = RowNote(RowZ(10),
            "半径 1 の球が毎フレーム +X へ 0.1 進み続ける。壁（Static・非トリガー）に\n"
            "当たったら押し戻され、中心 x = -2（壁面 -1 − 半径 1）で止まるはず。\n"
            "以前は判定結果が bool だけで法線も貫通深度も無く、押し出しが原理的に書けなかった。");
        // 1 フレームぶんの前進（0.1）より内側に収まっていれば押し出せている
        result.status = !decided ? Status::Pending
            : (std::abs(x - kPushExpectedX) < 0.05f) ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T13: CollisionInfo（法線・貫通深度）がコールバックまで届いているか
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateContactInfoCase()
    {
        if (!t5A_ || !t5B_) { return; }

        const bool decided = (frame_ >= kStaticJudge);
        const auto& stats = t5A_->Stats();

        // T5 の配置: 半径 1 の球が x = -0.6 と +0.6。中心間 1.2 → 貫通深度 0.8、
        // 法線は A(-0.6) から B(+0.6) へ = +X。
        constexpr float kExpectedDepth = 0.8f;

        char actual[128];
        std::snprintf(actual, sizeof(actual), "normal=(%.2f, %.2f, %.2f) depth=%.3f",
            stats.lastNormal.x, stats.lastNormal.y, stats.lastNormal.z, stats.lastDepth);

        const bool normalOk = std::abs(stats.lastNormal.x - 1.0f) < 1e-3f
                           && std::abs(stats.lastNormal.y) < 1e-3f
                           && std::abs(stats.lastNormal.z) < 1e-3f;
        const bool depthOk = std::abs(stats.lastDepth - kExpectedDepth) < 1e-3f;

        CaseResult result;
        result.id       = "T13";
        result.title    = "CollisionInfo（法線・貫通深度）がコールバックへ届く";
        result.expected = "normal=(1, 0, 0) depth=0.800";
        result.actual   = stats.lastInfoValid ? actual : "接触情報を受け取っていない";
        result.note     = RowNote(RowZ(4),
            "T5 と同じペア（半径 1 の球が中心間 1.2）。normal は自分から相手へ向かう向き。\n"
            "旧 API の OnCollisionEnter(GameObject*) も同時に呼ばれていること（T5 の\n"
            "カウンタが動いていること）で、後方互換の転送経路も検証している。");
        result.status = !decided ? Status::Pending
            : (stats.lastInfoValid && normalOk && depthOk) ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }

    // ─────────────────────────────────────────────────────────────────────
    // T14: レイキャスト問い合わせ（手前のコライダーが返るか）
    // ─────────────────────────────────────────────────────────────────────
    void CollisionTestScene::EvaluateRaycastQueryCase()
    {
        CollisionWorld* world = GetCollisionWorld();
        if (!t14Near_ || !t14Far_ || !world) { return; }

        const bool decided = (frame_ >= kStaticJudge);

        // x = -8 から +X 方向へ撃つ。手前の球（中心 -2・半径 1）の表面 x = -3 に当たるはず。
        const Geometry::Ray ray{ { -8.0f, kProbeY, RowZ(11) }, { 1.0f, 0.0f, 0.0f } };
        RaycastHit hit{};
        const bool found = world->Raycast(ray, 100.0f, CollisionWorld::kAllLayers, &hit);

        const bool hitNear = found && hit.object == t14Near_;
        const bool distanceOk = found && std::abs(hit.distance - 5.0f) < 1e-2f;

        char actual[128];
        if (found) {
            std::snprintf(actual, sizeof(actual), "%s に distance=%.3f でヒット",
                hit.object ? hit.object->GetDisplayName() : "(null)", hit.distance);
        } else {
            std::snprintf(actual, sizeof(actual), "ヒットなし");
        }

        CaseResult result;
        result.id       = "T14";
        result.title    = "Raycast が手前のコライダーを返す";
        result.expected = "T14_Near に distance=5.000";
        result.actual   = actual;
        result.knownBug = "B-7";
        result.note     = RowNote(RowZ(11),
            "x = -8 から +X へレイを撃つ。手前の球（中心 -2・半径 1）の表面 x = -3 に\n"
            "当たるので distance = 5。奥の球（中心 +3）を返したら最近ヒットの選択が壊れている。\n"
            "この 2 つは Item レイヤーでどのレイヤーとも衝突しないので、判定ループには\n"
            "現れない = 問い合わせ API が判定とは独立に動くことの確認にもなる。");
        result.status = !decided ? Status::Pending
            : (hitNear && distanceOk) ? Status::Pass : Status::Fail;
        Report::Get().Upsert(result);
    }
}

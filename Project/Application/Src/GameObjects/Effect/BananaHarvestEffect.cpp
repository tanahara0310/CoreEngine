#include "pch.h"
#include "BananaHarvestEffect.h"

#include "Components/Building/MapViewComponent.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "Components/UI/StaminaGaugeUIComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Components/Utility/ModelRenderPoolComponent.h"
#include "GameObjects/GameSceneObject.h"

#include "Audio/AudioSystem.h"
#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObjectManager.h"
#include "Math/Easing/EasingUtil.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Scene/Feature/ISceneFeature.h"
#include "UI/UIImage.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Random/Hash.h"
#include "WinApp/WinApp.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace CoreEngine;

namespace {

    // ──────────────────────────────────────────────────────────
    // バナナ収穫の演出
    // ──────────────────────────────────────────────────────────
    // サル 1 匹が木 1 本の隣を通るたびに、バナナが 1 本もぎ取られてそのサルの頭上へ飛ぶ。
    // 発火は HungerComponent が回復を確定した瞬間に届く（＝演出はゲームの状態を一切変えない）。
    //
    // ■ 連鎖は自前で組んでいない
    //   OnMonkeyEnteredCell() は列車が 1 マス進むたびに車両ごとへ呼ばれるので、
    //   1 本の木に対する発火はもともと時間的にばらけている。列車が長いほど
    //   「ポン…ポン…ポン」と収穫が長く続き、それがそのままサルの数の体感になる。
    //   自前でずらすのは、1 マスの四方が木で同じ瞬間に複数本取れたときだけ（Stagger）。
    //
    // ■ 大きさは回復量に比例させてある
    //   サルが増えるほど 1 匹あたりの回復量は逓減する（HungerComponent の補正）。
    //   全部同じ大きさで飛ばすと絵が実際より多く回復したと嘘をつくので、
    //   回復量の割合をそのままバナナの大きさへ掛ける。ただし MinSizeRate で下限を作ってある。
    //   数が増える気持ちよさまで削ると、サルが増えるほど地味になってしまう。
    //
    // ■ どの段階にも必ず動きを入れてある
    //   もぎ取り（膨らんで出現）→ 飛行（放物線・自転・伸縮）→ 着地（潰れて跳ね返る）
    //   → 掲げ（上下に揺れながら回る）→ 打ち上げ（沈んでから跳ね上がる）
    //   → HUD 飛行（2D）→ ゲージが弾む。
    //   止まって見えるフレームを作らないために、掲げの間も揺れと回転を止めていない。
    //
    // ■ 3D から 2D への受け渡し
    //   打ち上げの終わり（＝バナナが一番速く上がっている瞬間）で 3D モデルを消し、
    //   同じ画面位置・同じ画面上の大きさで 2D のバナナ（ゲージの粒と同じ pip.png）へ差し替える。
    //   大きさは「バナナの根元と先端をそれぞれ射影して、その距離を px で測る」ことで
    //   実測しているので、カメラの距離や画角が変わっても継ぎ目が出ない。
    //   形が変わる瞬間を一番速い瞬間に置き、さらに受け渡しの直後だけ軽く膨らませて
    //   （HandoffPunch）、差し替えを「変身」として読ませている。
    //
    // ■ ゲージの粒はバナナが着くより前に増えている
    //   スタミナは収穫した瞬間に確定するので、粒はそこで実る。ここで演出のために
    //   粒を遅らせると、ゲージが実際より少ない量を表示することになり、
    //   プレイヤーの「あと何マス敷けるか」の判断を狂わせる。
    //   なので着地は「加算の瞬間」ではなく「加算のダメ押し」にしてある。
    //   StaminaGaugeUIComponent::PlayGainPop() が、実っている粒を生え際から
    //   数粒ぶん伸び直させ、ゲージごと弾ませる。

    // ===== 有効・無効 =====

    CVar<bool> cvEnabled{
        "Game.BananaHarvest.Enabled", true,
        "サルがバナナの木を通ったときに、バナナの収穫演出を出す" };

    // ===== 大きさ =====

    CVar<float> cvSizeRate{
        "Game.BananaHarvest.SizeRate", 0.9f,
        "バナナの大きさ（マップチップと同じ共通スケールに対する倍率）。"
        "1.0 にすると 1 マスぶんの大きさで作られたモデルと同じ縮尺になる。"
        "既定の 0.9 で高さおよそ 0.62 マス（トロッコとほぼ同じ）。"
        "カメラが引いているので、小さくすると画面上で数ピクセルまで潰れて見えなくなる",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvMinSizeRate{
        "Game.BananaHarvest.MinSizeRate", 0.6f,
        "回復量が減ったときのバナナの最小倍率。"
        "サルが増えると 1 匹あたりの回復量は逓減するので、その割合を大きさへ掛けている。"
        "1.0 にすると回復量にかかわらず常に同じ大きさになる",
        CVarRange{ 0.1f, 1.0f } };

    // ===== 各段階の長さ =====

    CVar<float> cvPopDuration{
        "Game.BananaHarvest.PopDuration", 0.14f,
        "もぎ取られて木に現れるまでの秒数",
        CVarRange{ 0.01f, 1.0f } };

    CVar<float> cvFlyDuration{
        "Game.BananaHarvest.FlyDuration", 0.46f,
        "木からサルの頭上まで飛ぶ秒数",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvLandDuration{
        "Game.BananaHarvest.LandDuration", 0.16f,
        "頭に着いた瞬間の潰れ・跳ね返りの秒数",
        CVarRange{ 0.01f, 1.0f } };

    CVar<float> cvHoldDuration{
        "Game.BananaHarvest.HoldDuration", 0.35f,
        "頭上に掲げたままにする秒数。この間もバナナは上下に揺れて回り続ける。"
        "長くすると掲げているサルの数は読みやすくなるが、"
        "収穫からゲージが弾むまでが間延びして因果が繋がって見えなくなる",
        CVarRange{ 0.0f, 5.0f } };

    CVar<float> cvTuckDuration{
        "Game.BananaHarvest.TuckDuration", 0.26f,
        "掲げたバナナを引っ込めて消えるまでの秒数",
        CVarRange{ 0.01f, 2.0f } };

    CVar<float> cvStagger{
        "Game.BananaHarvest.Stagger", 0.08f,
        "同じ瞬間に複数本取れたとき（1 マスの四方が木のとき最大 4 本）の 1 本ごとのずらし秒数。"
        "0 にすると重なって 1 本に見える",
        CVarRange{ 0.0f, 0.5f } };

    // ===== 飛び方 =====

    CVar<float> cvArcHeight{
        "Game.BananaHarvest.ArcHeight", 0.85f,
        "飛ぶときの放物線の山の高さ（マス単位）。0 にすると直線で飛ぶ",
        CVarRange{ 0.0f, 3.0f } };

    CVar<float> cvFlySpin{
        "Game.BananaHarvest.FlySpin", 9.0f,
        "飛行中に軸まわりへ回る速さ[rad/秒]",
        CVarRange{ 0.0f, 40.0f } };

    CVar<float> cvFlyStretch{
        "Game.BananaHarvest.FlyStretch", 0.22f,
        "飛び出しで縦に伸び、着地に向けて縮む量。"
        "体積を保つように横方向は逆へ動かしている。0 で伸縮なし",
        CVarRange{ 0.0f, 0.8f } };

    // ===== 着地 =====

    CVar<float> cvLandSquash{
        "Game.BananaHarvest.LandSquash", 0.34f,
        "頭に着いた瞬間の潰れの深さ。潰れたあと同じ量だけ伸び上がって収まる",
        CVarRange{ 0.0f, 0.9f } };

    // ===== 掲げている間 =====

    CVar<float> cvHoldBobHeight{
        "Game.BananaHarvest.HoldBobHeight", 0.055f,
        "掲げている間に上下へ揺れる幅（マス単位）。0 にすると頭上で静止する",
        CVarRange{ 0.0f, 0.5f } };

    CVar<float> cvHoldBobSpeed{
        "Game.BananaHarvest.HoldBobSpeed", 7.0f,
        "掲げている間の上下の速さ[rad/秒]",
        CVarRange{ 0.0f, 30.0f } };

    CVar<float> cvHoldSpin{
        "Game.BananaHarvest.HoldSpin", 2.4f,
        "掲げている間に回り続ける速さ[rad/秒]。0 にすると向きが止まる",
        CVarRange{ 0.0f, 20.0f } };

    CVar<float> cvHoldSwing{
        "Game.BananaHarvest.HoldSwing", 0.16f,
        "掲げている間に左右へ傾く幅[rad]。手で持っているように見せるための揺れ",
        CVarRange{ 0.0f, 1.0f } };

    // ===== 頭上から離れるとき =====

    CVar<float> cvTuckLift{
        "Game.BananaHarvest.TuckLift", 0.14f,
        "頭上から離れる前に一度持ち上げる高さ（マス単位）。"
        "この予備動作が無いと、動き出しが唐突になる",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvTuckSpin{
        "Game.BananaHarvest.TuckSpin", 14.0f,
        "頭上から離れながら加速して回る速さ[rad/秒]",
        CVarRange{ 0.0f, 40.0f } };

    // ===== HUD（スタミナゲージ）へ飛ばす =====

    CVar<bool> cvHudEnabled{
        "Game.BananaHarvest.HudEnabled", true,
        "掲げたバナナをスタミナゲージへ飛ばして、ゲージを弾ませる。"
        "切ると頭上で縮んで消えるだけになる" };

    CVar<float> cvHudFlyDuration{
        "Game.BananaHarvest.HudFlyDuration", 0.5f,
        "2D になったバナナがゲージへ入るまでの秒数",
        CVarRange{ 0.05f, 3.0f } };

    CVar<float> cvHudArc{
        "Game.BananaHarvest.HudArc", 170.0f,
        "ゲージへ向かう軌道の膨らみ [px]（基準解像度 1920x1080）。"
        "0 にすると直線で滑っていくだけになる",
        CVarRange{ 0.0f, 800.0f } };

    CVar<float> cvHudLaunchRise{
        "Game.BananaHarvest.HudLaunchRise", 0.45f,
        "頭上から打ち上がる高さ（マス単位）。"
        "3D と 2D の継ぎ目を、バナナが一番速く動いている瞬間に置くための助走",
        CVarRange{ 0.0f, 3.0f } };

    CVar<float> cvHudHandoffPunch{
        "Game.BananaHarvest.HudHandoffPunch", 0.3f,
        "3D から 2D へ入れ替わった直後だけ膨らませる量。"
        "形が変わる瞬間を「変身」として読ませるための誤魔化し。0 で等倍のまま",
        CVarRange{ 0.0f, 1.5f } };

    CVar<float> cvHudSpin{
        "Game.BananaHarvest.HudSpin", 2.2f,
        "ゲージへ向かう間に画面内で首を振る回数。到着時は必ず粒と同じ向きで止まる",
        CVarRange{ 0.0f, 10.0f } };

    CVar<float> cvHudBrightness{
        "Game.BananaHarvest.HudBrightness", 0.6f,
        "飛んでいる 2D バナナの明るさ。UI はポストエフェクトより前に合成されるので、"
        "1 に近づけるとブルームで白く飛ぶ（ゲージの粒が 0.45 なのと同じ理由）",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> cvHudArriveFlash{
        "Game.BananaHarvest.HudArriveFlash", 0.8f,
        "ゲージへ入る直前に光る量。粒へ吸い込まれた感じを出す",
        CVarRange{ 0.0f, 3.0f } };

    // ===== 光り =====

    CVar<float> cvFlashStrength{
        "Game.BananaHarvest.FlashStrength", 0.7f,
        "もぎ取った瞬間と頭へ着いた瞬間に、バナナのベースカラーへ足す明るさ。"
        "0 にするとモデルそのままの色で出る",
        CVarRange{ 0.0f, 3.0f } };

    // ===== 音 =====

    CVar<bool> cvSeEnabled{
        "Game.BananaHarvest.SeEnabled", true,
        "収穫のたびに SE を鳴らす" };

    CVar<float> cvSeVolume{
        "Game.BananaHarvest.SeVolume", 0.35f,
        "収穫 SE の音量",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvSeBasePitch{
        "Game.BananaHarvest.SeBasePitch", 1.2f,
        "収穫 SE の基準ピッチ（先頭のサルが取ったとき）",
        CVarRange{ 0.25f, 4.0f } };

    CVar<float> cvSePitchStep{
        "Game.BananaHarvest.SePitchStep", 0.06f,
        "サル 1 匹ぶんごとに上げるピッチ（0.06 でおよそ半音）。"
        "列車の後ろへ行くほど音が上がって連鎖に聞こえる",
        CVarRange{ 0.0f, 0.5f } };

    CVar<int> cvSePitchSteps{
        "Game.BananaHarvest.SePitchSteps", 12,
        "ピッチを上げ続ける段数。ここを超えたら 1 段目へ戻る。"
        "列車が長くなっても音が高くなりすぎないようにするため",
        CVarRange{ 1, 48 } };

    // ──────────────────────────────────────────────────────────
    // モデルと定数
    // ──────────────────────────────────────────────────────────

    /// 飛ばすバナナのモデル
    /// @note 原点は房の根元（Y は 0.2〜1.3）。回転の支点も根元になるので、
    ///       Y 軸まわりの自転と Z 軸の首振りだけを使っている。
    ///       X 軸を大きく回すと、房が根元を中心に振り回されて手から離れて見える。
    constexpr const char* kBananaModel = "banana.obj";

    /// バナナを同時に何本まで描けるか。足りなければプールが自分で伸びる
    constexpr std::size_t kBananaPoolCapacity = 64;

    /// バナナの木モデル（banana_tree.obj、高さ 1.6）のうち、実が生っている高さ
    constexpr float kFruitHeight = 1.15f;

    /// 幹の中心からサル側へ実をずらす量（マス単位）。房から取れたように見せる
    constexpr float kFruitSideOffset = 0.3f;

    /// サル（monkey.obj、高さ 1.4）の頭のてっぺん。車両のスケールを掛けて使う
    constexpr float kMonkeyHeadHeight = 1.45f;

    /// もぎ取った反動で実が浮く高さ（マス単位）
    constexpr float kPopRise = 0.16f;

    /// 引っ込めるときに頭へ沈み込ませる深さ（マス単位）。HUD へ飛ばさない設定のときだけ使う
    constexpr float kTuckDepth = 0.2f;

    /// 飛んでいる 2D バナナに使うテクスチャ
    /// @note ゲージの粒と同じ絵にしてある。着地したところがそのまま粒になるので、
    ///       「このバナナがこの粒になった」が一目で繋がる。
    constexpr const char* kHudBananaTexture = "Application/Assets/Textures/Stamina/pip.png";

    /// 2D バナナを同時に何本出せるか。足りないぶんは 3D のまま消える（演出なので落とさない）
    constexpr std::size_t kHudImagePoolSize = 24;

    /// 2D バナナの描画順。ゲージ（既定 900）より手前に出して、粒へ重なりながら入る
    constexpr int kHudSortOrder = 960;

    /// バナナモデル（banana.obj）の絵が入っている範囲。原点からの高さで、房の下端と上端。
    /// 画面上の大きさを実測するときに、原点ではなく「実際に見えている部分」を測るために使う
    constexpr float kBananaModelBottom = 0.2f;
    constexpr float kBananaModelTop = 1.3f;

    /// 受け渡しの膨らみが収まるまでの割合（HUD 飛行のうち前半何割か）
    constexpr float kHandoffPunchSpan = 0.3f;

    /// ゲージへ入る直前の、光り始める割合
    constexpr float kArriveFlashSpan = 0.25f;

    /// 収穫 SE。バナナ専用の音が用意できるまでの仮。Game.BananaHarvest.SeEnabled で切れる
    constexpr const char* kHarvestSe = "Application/Assets/Sounds/SE/decision.mp3";

    // ──────────────────────────────────────────────────────────
    // 1 本ぶんの状態
    // ──────────────────────────────────────────────────────────

    /// @brief 飛んでいる（あるいは掲げられている）バナナ 1 本
    struct Banana {
        /// 掲げる先のサルが乗っている車両。連結された車両はシーンと同じだけ生きる
        const TransformComponent* carriage = nullptr;
        /// もぎ取られた実の位置（ワールド）
        Vector3 origin{};
        /// 同じ瞬間に複数本出たときのずらし秒数
        float delay = 0.0f;
        /// 再生開始からの経過秒
        float elapsed = 0.0f;
        /// 回復量に応じた大きさの倍率
        float sizeRate = 1.0f;
        /// 掲げている間の揺れの位相。バナナごとにずらす
        float swayPhase = 0.0f;
        /// 積み上がった自転角。段階をまたいでも巻き戻らないように保持する
        float spin = 0.0f;
        /// この 1 本で回復したスタミナ量。ゲージを弾ませる粒の数に使う
        float amount = 0.0f;

        // ── 3D から 2D へ受け渡すときに 1 度だけ測る ──
        bool handoffDone = false;   ///< 受け渡しの計測を済ませたか
        bool handoffValid = false;  ///< 画面内で測れたか。カメラの後ろだと false
        Vector2 hudStart{};         ///< 受け渡した瞬間の画面位置 [px]
        float hudStartHeight = 0.0f;///< 受け渡した瞬間の画面上の高さ [px]
        float hudStartAngle = 0.0f; ///< 受け渡した瞬間の画面上の傾き [rad]
        bool arrived = false;       ///< ゲージへ着いてゲージを弾ませたか
    };

    /// @brief 1 本ぶんの見た目。毎フレーム計算して ModelRenderPool へ渡す
    struct BananaPose {
        Vector3 position{};
        Vector3 rotation{};
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        float flash = 0.0f; ///< ベースカラーへ足す明るさ
        bool visible = false;
    };

    /// @brief 段階の境目（秒）。CVar を触ると変わるので、毎フレーム作り直す
    struct Timeline {
        float pop = 0.0f;  ///< もぎ取り終わり
        float fly = 0.0f;  ///< 飛行終わり
        float land = 0.0f; ///< 着地終わり
        float hold = 0.0f; ///< 掲げ終わり
        float exit = 0.0f; ///< 頭上から離れ終わり（＝3D で描く最後）
        float hud = 0.0f;  ///< ゲージへ着く時刻（＝寿命）。HUD を切ると exit と同じ
        bool  toHud = false; ///< ゲージへ飛ばす設定か
    };

    Timeline MakeTimeline()
    {
        Timeline timeline;
        timeline.pop = std::max(cvPopDuration.Get(), 0.01f);
        timeline.fly = timeline.pop + std::max(cvFlyDuration.Get(), 0.01f);
        timeline.land = timeline.fly + std::max(cvLandDuration.Get(), 0.01f);
        timeline.hold = timeline.land + std::max(cvHoldDuration.Get(), 0.0f);
        timeline.exit = timeline.hold + std::max(cvTuckDuration.Get(), 0.01f);
        timeline.toHud = cvHudEnabled.Get();
        timeline.hud = timeline.exit +
            (timeline.toHud ? std::max(cvHudFlyDuration.Get(), 0.01f) : 0.0f);
        return timeline;
    }

    /// @brief 0..1 へ正規化する。長さが 0 でも落ちないようにする
    float Normalize(float value, float length)
    {
        return length > 0.0f ? std::clamp(value / length, 0.0f, 1.0f) : 1.0f;
    }

    /// @brief 体積を保ったまま縦へ伸び縮みさせる係数を作る
    /// @param stretch 1.0 で等倍。1 より大きいと縦に伸びて横が細くなる
    Vector3 StretchFactor(float stretch)
    {
        const float safeStretch = std::max(stretch, 0.05f);
        const float width = 1.0f / std::sqrt(safeStretch);
        return { width, safeStretch, width };
    }

    /// @brief ワールド座標を、UI と同じ基準解像度（1920x1080）の px へ落とす
    /// @return カメラの後ろにある点は測れないので false
    /// @note 描画ターゲットの実寸ではなく基準解像度で返すのは、UI 側の座標が
    ///       そこで固定されているため（表示はレターボックスで合わせられる）。
    bool WorldToCanvas(const Camera& camera, const Vector3& world, Vector2& outPosition)
    {
        const Matrix4x4 viewProjection = camera.GetViewMatrix() * camera.GetProjectionMatrix();
        const Vector4 clip = MathCore::CoordinateTransform::TransformCoord(
            Vector4{ world.x, world.y, world.z, 1.0f }, viewProjection);

        // w <= 0 はカメラの後ろ。割ると符号が反転して、背後の点が画面内へ出てしまう
        if (clip.w <= 1.0e-5f) {
            return false;
        }

        // TAA のジッタは射影行列へ NDC 単位で足してある（clip.xy += jitter * clip.w）。
        // UI はジッタを受けないので、ここで抜かないと 2D バナナだけ 1px 未満で震える。
        const float ndcX = clip.x / clip.w - camera.GetProjectionJitterX();
        const float ndcY = clip.y / clip.w - camera.GetProjectionJitterY();

        outPosition = {
            (ndcX * 0.5f + 0.5f) * static_cast<float>(WinApp::kReferenceWidth),
            // NDC の Y は上が正、画面座標は下が正なので反転する
            (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(WinApp::kReferenceHeight) };
        return true;
    }

    /// @brief 2 次ベジエ。ゲージへ向かう軌道に使う
    Vector2 QuadraticBezier(const Vector2& from, const Vector2& control, const Vector2& to, float t)
    {
        const float inv = 1.0f - t;
        const float a = inv * inv;
        const float b = 2.0f * inv * t;
        const float c = t * t;
        return {
            from.x * a + control.x * b + to.x * c,
            from.y * a + control.y * b + to.y * c };
    }

    // ──────────────────────────────────────────────────────────
    // Feature
    // ──────────────────────────────────────────────────────────

    /// @brief バナナの収穫演出をシーンへ 1 つ置いておく Feature
    /// @details HungerComponent から収穫の通知を受け取り、バナナを 1 本ぶん飛ばす。
    ///          ゲームの状態は読むだけで、書き換えは一切しない。
    class BananaHarvestEffectFeature final : public ISceneFeature {
    public:
        const char* GetName() const override { return "BananaHarvestEffect"; }

        /// @details シーンの OnInitialize() が終わった後のフックなので、
        ///          この時点ならスタミナ・列車・マップのコンポーネントが揃っている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            hunger_ = ctx.gameObjectManager->FindFirstComponent<GameComponents::HungerComponent>();
            train_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::TrainMovementComponent>();
            // 木を揺らすために見ている。無くてもバナナは飛ぶ
            mapView_ = ctx.gameObjectManager->FindFirstComponent<GameComponents::MapViewComponent>();
            // ゲージは StaminaGaugeFeature が先に作っている（登録順が先なので PostSceneInitialize も先）。
            // 見つからなければ HUD へは飛ばさず、頭上で消えるところまでを出す。
            gauge_ =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::StaminaGaugeUIComponent>();
            audio_ = ctx.engine ? ctx.engine->GetService<AudioSystem>() : nullptr;

            if (!hunger_ || !train_) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "BananaHarvestEffect: 必要なコンポーネントが見つからないため演出を出しません");
                return;
            }

            pool_ = CreateBananaPool(ctx);
            if (!pool_) {
                return;
            }
            CreateHudImages(ctx);

            hunger_->SetBananaHarvestCallback(
                [this](const GameComponents::BananaHarvestEvent& harvest) {
                    OnHarvested(harvest);
                });
        }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override
        {
            // 車両の位置が確定してから動かす。頭上のバナナを 1 フレーム遅れさせないため。
            // ゲージ（コンポーネント）の配置もこの時点で今フレームぶんが済んでいる。
            if (phase != SceneUpdatePhase::PostObjectUpdate || !pool_) {
                return;
            }

            const float deltaTime = std::max(Time::DeltaTime(), 0.0f);
            const Timeline timeline = MakeTimeline();
            const Camera* camera = ctx.gameViewCamera3D;

            for (Banana& banana : bananas_) {
                banana.elapsed += deltaTime;
            }

            // 寿命が尽きたものは、消す前にゲージを弾ませる。
            // 消してから鳴らすと、同じフレームで消えたぶんの通知が抜ける。
            for (Banana& banana : bananas_) {
                if (banana.arrived || banana.elapsed - banana.delay < timeline.hud) {
                    continue;
                }
                banana.arrived = true;
                if (gauge_) {
                    gauge_->PlayGainPop(banana.amount);
                }
            }
            std::erase_if(bananas_, [](const Banana& banana) { return banana.arrived; });

            std::size_t hudCount = 0;
            for (Banana& banana : bananas_) {
                const float time = banana.elapsed - banana.delay;
                if (time < timeline.exit) {
                    const BananaPose pose = EvaluatePose(banana, timeline, deltaTime);
                    if (pose.visible) {
                        pool_->Draw(
                            pose.position, pose.rotation, pose.scale, MakeTint(pose.flash));
                    }
                    continue;
                }

                // ここから先は 2D。3D モデルは描かない（＝この Draw を飛ばすと自動で消える）
                if (!banana.handoffDone) {
                    CaptureHandoff(banana, timeline, camera);
                }
                if (banana.handoffValid && hudCount < hudImages_.size()) {
                    DrawHudBanana(banana, timeline, hudImages_[hudCount]);
                    ++hudCount;
                }
            }

            // 使わなかった 2D バナナは畳む
            for (std::size_t i = hudCount; i < hudImages_.size(); ++i) {
                if (hudImages_[i] && hudImages_[i]->IsActive()) {
                    hudImages_[i]->SetActive(false);
                }
            }
        }

        /// @details GameObject が消える前に、スタミナ側が握っているコールバックを外す。
        ///          Feature はシーンより後に壊れるので、外さないと解放済みの this を呼びうる。
        void Finalize(SceneContext&) override
        {
            if (hunger_) {
                hunger_->SetBananaHarvestCallback(nullptr);
            }
            bananas_.clear();
            hudImages_.clear();
            hunger_ = nullptr;
            train_ = nullptr;
            mapView_ = nullptr;
            gauge_ = nullptr;
            pool_ = nullptr;
            audio_ = nullptr;
        }

    private:
        /// @brief バナナを描くモデルプールを 1 つ作る
        static GameComponents::ModelRenderPoolComponent* CreateBananaPool(SceneContext& ctx)
        {
            auto* owner = ctx.gameObjectManager->AddObject(
                std::make_unique<GameScene::GameSceneObject>("BananaHarvestPool"));
            if (!owner) {
                Logger::GetInstance().Errorf(
                    LogCategory::Game,
                    "BananaHarvestEffect: バナナのプールを作れませんでした");
                return nullptr;
            }
            // 演出のためにここで作ったオブジェクトなので、シーンの JSON には残さない
            owner->SetSerializeEnabled(false);
            owner->AddComponent<TransformComponent>();
            return owner->AddComponent<GameComponents::ModelRenderPoolComponent>(
                kBananaModel, kBananaPoolCapacity, true);
        }

        /// @brief 2D になったバナナを描く UI をあらかじめ作っておく
        void CreateHudImages(SceneContext& ctx)
        {
            hudImages_.reserve(kHudImagePoolSize);
            for (std::size_t i = 0; i < kHudImagePoolSize; ++i) {
                auto* image = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
                if (!image) {
                    break;
                }
                image->Initialize(
                    kHudBananaTexture, "BananaHarvestHud_" + std::to_string(i));
                image->SetSerializeEnabled(false);
                image->SetAnchor(UIAnchor::TopLeft);
                // 回転と拡縮を中心まわりで効かせたいので、軸は真ん中に置く
                image->SetPivot({ 0.5f, 0.5f });
                image->SetSortOrder(kHudSortOrder);
                image->SetActive(false);
                hudImages_.push_back(image);
            }
        }

        /// @brief 3D から 2D へ受け渡す瞬間の画面位置と大きさを 1 度だけ測る
        /// @details バナナの根元と先端をそれぞれ射影し、その画面上の距離を高さとして使う。
        ///          こうしておくと、カメラの距離や画角が変わっても継ぎ目で大きさが飛ばない。
        void CaptureHandoff(Banana& banana, const Timeline& timeline, const Camera* camera)
        {
            banana.handoffDone = true;
            banana.handoffValid = false;
            if (!timeline.toHud || !camera || !gauge_ || !banana.carriage) {
                return;
            }

            // 受け渡しの瞬間（打ち上げの終わり）に 3D バナナがいた場所を作り直す
            const float gridSize = std::max(GameComponents::GameSettings::GridSize.Get(), 0.01f);
            const float modelScale = GameComponents::BlockModelLayout::GetScale(gridSize);
            const float baseScale =
                modelScale * std::max(cvSizeRate.Get(), 0.01f) * banana.sizeRate;
            const Vector3 head = HeadPosition(*banana.carriage);
            const float launchY = head.y + cvHudLaunchRise.Get() * gridSize;
            // 房の下端と上端。モデルの原点は房より下にあるので、そのまま測ると
            // 見えているバナナより大きい高さになってしまう
            const Vector3 root{ head.x, launchY + kBananaModelBottom * baseScale, head.z };
            const Vector3 tip{ head.x, launchY + kBananaModelTop * baseScale, head.z };

            Vector2 rootCanvas{};
            Vector2 tipCanvas{};
            if (!WorldToCanvas(*camera, root, rootCanvas) ||
                !WorldToCanvas(*camera, tip, tipCanvas)) {
                // カメラの後ろ。測れないので 2D へは出さない（ゲージだけ弾ませる）
                return;
            }

            const float dx = tipCanvas.x - rootCanvas.x;
            const float dy = tipCanvas.y - rootCanvas.y;
            const float height = std::sqrt(dx * dx + dy * dy);
            if (height < 1.0f) {
                // 画面上で 1px 未満。継ぎ目を作る意味がないので 2D は省く
                return;
            }

            banana.handoffValid = true;
            // 3D バナナの見た目の中心は根元と先端の中間。2D 側は軸が中心なのでそこへ置く
            banana.hudStart = {
                (rootCanvas.x + tipCanvas.x) * 0.5f,
                (rootCanvas.y + tipCanvas.y) * 0.5f };
            banana.hudStartHeight = height;
            // 画面上でどれだけ傾いて見えていたか。2D 側はここから始めて、粒と同じ向きへ収める
            banana.hudStartAngle = std::atan2(dx, -dy);
        }

        /// @brief 2D になったバナナを 1 本ぶん描く
        void DrawHudBanana(const Banana& banana, const Timeline& timeline, UIImage* image) const
        {
            if (!image || !gauge_) {
                return;
            }

            Vector2 target{};
            Vector2 pipSize{};
            if (!gauge_->TryGetFillFrontTarget(target, pipSize)) {
                if (image->IsActive()) {
                    image->SetActive(false);
                }
                return;
            }

            const float time = banana.elapsed - banana.delay;
            const float progress =
                Normalize(time - timeline.exit, timeline.hud - timeline.exit);

            // 軌道。まっすぐ滑らせると「移動しているだけ」に見えるので、
            // いったん上へ膨らませてからゲージへ落とす。
            const Vector2 control{
                std::lerp(banana.hudStart.x, target.x, 0.3f),
                std::lerp(banana.hudStart.y, target.y, 0.3f) - cvHudArc.Get() };
            // 出だしは打ち上げの勢いを引き継いで速く、ゲージの手前で吸い込まれるように落ち着かせる
            const float travel = EasingUtil::Apply(progress, EasingUtil::Type::EaseInOutSine);
            const Vector2 position = QuadraticBezier(banana.hudStart, control, target, travel);

            // 大きさ。受け渡しの瞬間は 3D の実測値ぴったりで、そこから粒の大きさへ寄せる。
            // 直後だけ膨らませて、形が入れ替わったことを「変身」として読ませる。
            const float shrink = EasingUtil::Apply(progress, EasingUtil::Type::EaseInQuad);
            const float height = std::lerp(banana.hudStartHeight, pipSize.y, shrink);
            const float punch = 1.0f + cvHudHandoffPunch.Get() *
                std::sin(Normalize(progress, kHandoffPunchSpan) * std::numbers::pi_v<float>);
            const float aspect = pipSize.y > 0.0f ? pipSize.x / pipSize.y : 0.33f;
            image->SetSize({ height * aspect * punch, height * punch });

            // 向き。受け渡し時の傾きから、粒と同じ真っ直ぐへ収める。
            // 途中は減衰する首振りを乗せて、飛んでいる間も止まって見えないようにする
            const float settle = EasingUtil::Apply(progress, EasingUtil::Type::EaseOutCubic);
            const float wobble =
                std::sin(progress * cvHudSpin.Get() * 2.0f * std::numbers::pi_v<float>) *
                0.35f * (1.0f - progress);
            image->SetUIRotation(banana.hudStartAngle * (1.0f - settle) + wobble);

            // ゲージへ入る直前だけ光らせて、粒へ吸い込まれた感じを出す
            const float arrive = Normalize(progress - (1.0f - kArriveFlashSpan), kArriveFlashSpan);
            const float brightness =
                cvHudBrightness.Get() * (1.0f + arrive * arrive * cvHudArriveFlash.Get());
            image->SetAnchoredPosition(position);
            image->SetColor({ brightness, brightness, brightness * 0.95f, 1.0f });
            if (!image->IsActive()) {
                image->SetActive(true);
            }
        }

        /// @brief 収穫 1 本ぶんの通知を受け取り、バナナを 1 本足す
        void OnHarvested(const GameComponents::BananaHarvestEvent& harvest)
        {
            if (!cvEnabled.Get() || !train_) {
                return;
            }
            const auto* carriage = train_->GetMonkeyTransform(harvest.monkeyIndex);
            if (!carriage) {
                return;
            }

            const float gridSize = std::max(GameComponents::GameSettings::GridSize.Get(), 0.01f);
            const float modelScale = GameComponents::BlockModelLayout::GetScale(gridSize);
            const float surfaceHeight = GameComponents::BlockModelLayout::GetSurfaceHeight(gridSize);

            // 幹の中心ではなく、サルに近い側の房から取れたように見せる
            const float towardX = static_cast<float>(harvest.monkeyGridX - harvest.treeGridX);
            const float towardZ = static_cast<float>(harvest.monkeyGridZ - harvest.treeGridZ);

            Banana banana;
            banana.carriage = carriage;
            banana.origin = {
                (static_cast<float>(harvest.treeGridX) + towardX * kFruitSideOffset) * gridSize,
                surfaceHeight + kFruitHeight * modelScale,
                (static_cast<float>(harvest.treeGridZ) + towardZ * kFruitSideOffset) * gridSize };
            banana.delay =
                static_cast<float>(harvest.indexInFrame) * std::max(cvStagger.Get(), 0.0f);
            // 木とサルの組で向きを散らす。同じ絵が並ばないようにするだけなので値ノイズでよい
            banana.swayPhase = Hash::Cell01(
                harvest.treeGridX + static_cast<std::int32_t>(harvest.monkeyIndex),
                harvest.treeGridZ) * 2.0f * std::numbers::pi_v<float>;
            banana.spin = banana.swayPhase;
            // 回復量が減るほど小さくする。ただし下限までで、数が増える気持ちよさは残す
            banana.sizeRate = std::max(
                std::clamp(harvest.amountRate, 0.0f, 1.0f),
                std::clamp(cvMinSizeRate.Get(), 0.0f, 1.0f));
            banana.amount = harvest.amount;
            bananas_.push_back(banana);

            if (mapView_) {
                mapView_->PlayBananaTreeShake(
                    harvest.treeGridX, harvest.treeGridZ, towardX, towardZ);
            }
            PlayHarvestSe(harvest.monkeyIndex);
        }

        /// @brief サルの番号ぶんピッチを上げて鳴らす。列車の後ろほど高くなる
        void PlayHarvestSe(std::size_t monkeyIndex) const
        {
            if (!audio_ || !cvSeEnabled.Get()) {
                return;
            }
            const auto steps = static_cast<std::size_t>(std::max(cvSePitchSteps.Get(), 1));
            const auto step = static_cast<float>(monkeyIndex % steps);
            PlayParams params;
            params.bus = AudioBus::SE;
            params.volume = cvSeVolume.Get();
            params.pitch = std::max(cvSeBasePitch.Get() + step * cvSePitchStep.Get(), 0.25f);
            audio_->PlayOneShot(kHarvestSe, params);
        }

        /// @brief 明るさの上乗せをベースカラーへ変換する
        static std::optional<Vector4> MakeTint(float flash)
        {
            if (flash <= 0.0f) {
                return std::nullopt;
            }
            // アルファは触らない。バナナは不透明のまま、黄へ寄せて明るくするだけ
            return Vector4{ 1.0f + flash, 1.0f + flash * 0.85f, 1.0f + flash * 0.4f, 1.0f };
        }

        /// @brief サルの頭のてっぺんを求める
        /// @note 車両のスケールを掛けているので、連結直後の出現演出で車両が潰れている間も
        ///       バナナが頭から浮かない。
        static Vector3 HeadPosition(const TransformComponent& carriage)
        {
            const Vector3 base = carriage.GetWorldPosition();
            return { base.x, base.y + kMonkeyHeadHeight * carriage.Get().scale.y, base.z };
        }

        /// @brief バナナ 1 本の、このフレームの見た目を決める
        BananaPose EvaluatePose(Banana& banana, const Timeline& timeline, float deltaTime) const
        {
            BananaPose pose;
            const float time = banana.elapsed - banana.delay;
            if (time < 0.0f || !banana.carriage) {
                // ずらし待ちの間は出さない。ここで描くと 1 本目と同時に見えてしまう
                return pose;
            }
            pose.visible = true;

            const float gridSize = std::max(GameComponents::GameSettings::GridSize.Get(), 0.01f);
            const float modelScale = GameComponents::BlockModelLayout::GetScale(gridSize);
            const float baseScale =
                modelScale * std::max(cvSizeRate.Get(), 0.01f) * banana.sizeRate;
            const Vector3 head = HeadPosition(*banana.carriage);

            Vector3 stretch{ 1.0f, 1.0f, 1.0f };
            float sizeFade = 1.0f;
            float swing = 0.0f;

            if (time < timeline.pop) {
                // ── もぎ取り。実の位置で 0 から膨らみ、行き過ぎて戻る
                const float progress = Normalize(time, timeline.pop);
                sizeFade = EasingUtil::Apply(progress, EasingUtil::Type::EaseOutBack);
                pose.position = banana.origin;
                // ちぎれた反動でわずかに浮く
                pose.position.y +=
                    EasingUtil::Apply(progress, EasingUtil::Type::EaseOutQuad) * kPopRise * gridSize;
                // 出た瞬間だけ強く回して、木から弾かれた勢いを見せる
                banana.spin += cvFlySpin.Get() * 1.6f * deltaTime;
                swing = std::sin(progress * std::numbers::pi_v<float>) * 0.55f;
                pose.flash = cvFlashStrength.Get() * (1.0f - progress);
            } else if (time < timeline.fly) {
                // ── 飛行。放物線でサルの頭上へ
                const float progress = Normalize(time - timeline.pop, timeline.fly - timeline.pop);
                const float horizontal = EasingUtil::Apply(progress, EasingUtil::Type::EaseOutQuad);
                const Vector3 from{
                    banana.origin.x, banana.origin.y + kPopRise * gridSize, banana.origin.z };
                pose.position = from + (head - from) * horizontal;
                pose.position.y +=
                    std::sin(progress * std::numbers::pi_v<float>) * cvArcHeight.Get() * gridSize;
                banana.spin += cvFlySpin.Get() * deltaTime;
                // 弧に沿って前後へ傾ける。上りで反り、下りで前へ倒れる
                swing = std::cos(progress * std::numbers::pi_v<float>) * 0.45f;
                // 飛び出しで縦に伸び、着地へ向けて縮む
                stretch = StretchFactor(
                    1.0f + std::cos(progress * std::numbers::pi_v<float>) * cvFlyStretch.Get());
            } else if (time < timeline.land) {
                // ── 着地。頭に着いた瞬間に潰れ、跳ね返って収まる
                const float progress = Normalize(time - timeline.fly, timeline.land - timeline.fly);
                // 減衰する正弦波 1 周期ぶん。前半が潰れ、後半が伸び上がり
                const float recoil =
                    std::sin(progress * 2.0f * std::numbers::pi_v<float>) * (1.0f - progress);
                pose.position = head;
                stretch = StretchFactor(1.0f - recoil * cvLandSquash.Get());
                // 飛行の回転から掲げの回転へ、速度を落としながら繋ぐ
                banana.spin += std::lerp(cvFlySpin.Get(), cvHoldSpin.Get(), progress) * deltaTime;
                swing = 0.45f * (1.0f - progress);
                pose.flash = cvFlashStrength.Get() * 0.8f * (1.0f - progress);
            } else if (time < timeline.hold) {
                // ── 掲げ。止まって見えないよう、上下・首振り・自転を止めない
                const float sway = time * cvHoldBobSpeed.Get() + banana.swayPhase;
                pose.position = head;
                pose.position.y += std::sin(sway) * cvHoldBobHeight.Get() * gridSize;
                // 上下と同位相でわずかに伸縮させる。持ち上げで伸び、沈みで潰れる
                stretch = StretchFactor(1.0f + std::sin(sway) * 0.06f);
                banana.spin += cvHoldSpin.Get() * deltaTime;
                swing = std::sin(sway * 0.5f) * cvHoldSwing.Get();
            } else if (timeline.toHud) {
                // ── 打ち上げ。いったん頭へ沈み込んでから跳ね上がる（予備動作つき）。
                //    3D と 2D の継ぎ目を、バナナが一番速く上がっている瞬間へ置きたいので、
                //    この段の終わりで速度が最大になるよう EaseInQuad で加速させている。
                const float progress =
                    Normalize(time - timeline.hold, timeline.exit - timeline.hold);
                const float crouch = Normalize(progress, 0.35f);
                const float rise = Normalize(progress - 0.35f, 0.65f);
                pose.position = head;
                pose.position.y +=
                    // 沈み込み（0 → -lift → 0）。ここが跳ね上がりの助走になる
                    -std::sin(crouch * std::numbers::pi_v<float>) * cvTuckLift.Get() * gridSize +
                    EasingUtil::Apply(rise, EasingUtil::Type::EaseInQuad) *
                        cvHudLaunchRise.Get() * gridSize;
                // 沈むときに潰れ、上がるときに伸びる
                stretch = StretchFactor(
                    1.0f - std::sin(crouch * std::numbers::pi_v<float>) * 0.22f +
                    rise * 0.25f);
                banana.spin += std::lerp(cvHoldSpin.Get(), cvTuckSpin.Get(), rise) * deltaTime;
                swing = std::sin(rise * std::numbers::pi_v<float>) * cvHoldSwing.Get() * 1.5f;
            } else {
                // ── 収納。ゲージへ飛ばさない設定のとき。
                //    一度クイッと持ち上げてから、縮みながら頭へ吸い込ませる
                const float progress =
                    Normalize(time - timeline.hold, timeline.exit - timeline.hold);
                // 前 3 割が予備動作、残りが引き込み
                const float lift = Normalize(progress, 0.3f);
                const float sink = Normalize(progress - 0.3f, 0.7f);
                pose.position = head;
                pose.position.y +=
                    EasingUtil::Apply(lift, EasingUtil::Type::EaseOutBack) *
                        cvTuckLift.Get() * gridSize -
                    EasingUtil::Apply(sink, EasingUtil::Type::EaseInCubic) *
                        (cvTuckLift.Get() + kTuckDepth) * gridSize;
                // EaseInBack は序盤で負へ振れる。1 から引くと、縮む前に一度ふくらむ
                sizeFade =
                    std::max(1.0f - EasingUtil::Apply(sink, EasingUtil::Type::EaseInBack), 0.0f);
                // 消え際は回転を上げる。最後の 1 フレームまで動きを止めない
                banana.spin += std::lerp(cvHoldSpin.Get(), cvTuckSpin.Get(), sink) * deltaTime;
                swing = std::sin(sink * std::numbers::pi_v<float>) * cvHoldSwing.Get() * 1.5f;
            }

            const float scale = baseScale * sizeFade;
            pose.scale = { scale * stretch.x, scale * stretch.y, scale * stretch.z };
            // 原点が房の根元にあるモデルなので、大きく倒すのは Z（首振り）だけにしている
            pose.rotation = { 0.0f, banana.spin, swing };
            return pose;
        }

        GameComponents::HungerComponent* hunger_ = nullptr;
        GameComponents::TrainMovementComponent* train_ = nullptr;
        GameComponents::MapViewComponent* mapView_ = nullptr;
        GameComponents::StaminaGaugeUIComponent* gauge_ = nullptr;
        GameComponents::ModelRenderPoolComponent* pool_ = nullptr;
        AudioSystem* audio_ = nullptr;

        std::vector<Banana> bananas_;
        /// 2D になったバナナを描く UI。所有は GameObjectManager
        std::vector<UIImage*> hudImages_;
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateBananaHarvestEffectFeature()
{
    return std::make_unique<BananaHarvestEffectFeature>();
}

#include "pch.h"
#include "TrolleyLoading.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/TextureManager.h"
#include "Utility/CVar/CVar.h"
#include "Utility/FrameRate/Time.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
#include <cassert>
#include <algorithm>
#include <cmath>


namespace CoreEngine
{
    namespace
    {
        CVar<float> cvBobSpeed{
            "r.TrolleyLoading.BobSpeed", 336.0f,
            "トロッコが跳ねる速さ（縦 1080 基準の px/秒。枕木 1 本ぶんで 1 回跳ねる換算）",
            CVarRange{ 40.0f, 1200.0f } };

        CVar<float> cvRailScroll{
            "r.TrolleyLoading.RailScroll", 0.0f,
            "レールと奥の景色が流れる速さ（0 でカメラを世界に固定し、トロッコだけが走る）",
            CVarRange{ 0.0f, 1200.0f } };

        CVar<float> cvParallax{
            "r.TrolleyLoading.Parallax", 0.32f,
            "奥の景色が流れる速さの比（1.0 で手前と同速）。RailScroll が 0 なら効かない",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvScale{
            "r.TrolleyLoading.Scale", 0.72f,
            "全体の拡大率。絵の大きさも配置の距離も一括で変わる（速さの見え方も追従する）",
            CVarRange{ 0.2f, 2.0f } };

        CVar<float> cvRailY{
            "r.TrolleyLoading.RailY", 0.87f,
            "レール上端の位置（画面高さに対する比率）",
            CVarRange{ 0.3f, 0.98f } };

        CVar<float> cvCartX{
            "r.TrolleyLoading.CartX", -0.14f,
            "進捗 0 のときのトロッコ左端（画面幅に対する比率。負で画面外から発車する）",
            CVarRange{ -0.6f, 0.8f } };

        CVar<float> cvCartGoalX{
            "r.TrolleyLoading.CartGoalX", 0.73f,
            "進捗 1 のときのトロッコ左端（画面幅に対する比率）",
            CVarRange{ 0.0f, 1.2f } };

        CVar<float> cvBobAmp{
            "r.TrolleyLoading.BobAmp", 4.0f,
            "枕木を通過するたびに跳ねる上下幅（縦 1080 基準の px）",
            CVarRange{ 0.0f, 20.0f } };

        CVar<float> cvTiltDegrees{
            "r.TrolleyLoading.TiltDegrees", 1.6f,
            "車体が前後に傾く角度",
            CVarRange{ 0.0f, 10.0f } };

        CVar<float> cvCartLift{
            "r.TrolleyLoading.CartLift", 0.0f,
            "レール上端から車体下端までの距離（0 でレールの上に載る。上げると浮く）",
            CVarRange{ -40.0f, 160.0f } };

        CVar<float> cvStationGoal{
            "r.TrolleyLoading.StationGoal", 260.0f,
            "進捗 1.0 で駅が来る位置（到着したトロッコの左端からの距離）",
            CVarRange{ 0.0f, 1200.0f } };

        CVar<float> cvStationDrop{
            "r.TrolleyLoading.StationDrop", 8.0f,
            "レール上端から駅の下端までの距離",
            CVarRange{ -40.0f, 80.0f } };

        CVar<float> cvSceneryDrop{
            "r.TrolleyLoading.SceneryDrop", 4.0f,
            "レール上端から奥の景色の下端までの距離",
            CVarRange{ -80.0f, 80.0f } };

        CVar<float> cvTextScale{
            "r.TrolleyLoading.TextScale", 1.0f,
            "「ローディング中…」の拡大率（1.0 で焼いたままの大きさ。トロッコの Scale とは独立）",
            CVarRange{ 0.2f, 3.0f } };

        CVar<float> cvTextY{
            "r.TrolleyLoading.TextY", 0.5f,
            "「ローディング中…」の中心の高さ（画面高さに対する比率）",
            CVarRange{ 0.05f, 0.95f } };

        CVar<float> cvDotInterval{
            "r.TrolleyLoading.DotInterval", 0.35f,
            "点が 1 つ増える間隔（秒）。文字の明滅もこの 4 倍を 1 周期にして揃う",
            CVarRange{ 0.05f, 1.5f } };

        CVar<float> cvTextGap{
            "r.TrolleyLoading.TextGap", 21.0f,
            "文字列の右端から最初の点までの距離（縦 1080 基準の px）",
            CVarRange{ 0.0f, 200.0f } };

        // 表示強度は SceneTransition が遷移のたびに切り替える実行時状態のため保存しない
        CVar<bool> cvEnabled{
            "r.TrolleyLoading.Enabled", false,
            "トロッコのローディング画面を有効にする（通常は SceneTransition が自動で切り替える）",
            CVarRange{}, CVarFlags::NoSave | CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.TrolleyLoading";

        // スプライト。.obj から正射投影で焼いたもの（縦 1080 基準の大きさ）
        constexpr const char* kCartTexture    = "loading_cart.png";
        constexpr const char* kRailTexture    = "loading_rail.png";
        constexpr const char* kStationTexture = "loading_station.png";
        constexpr const char* kSceneryTexture = "loading_scenery.png";
        // 「ローディング中」。ドット絵フォント（x8y12pxDenkiChip）を 84px で焼いたもの。
        // 縦 1080 基準の大きさなので、他のスプライトと同じ扱いで置ける
        constexpr const char* kTextTexture    = "loading_text.png";

        // 1 フレームで進める時間の上限。読み込み中はコマ落ちするので、
        // 大きなデルタをそのまま積むとトロッコが飛ぶ。
        // ただし絞りすぎると重いフレームで「飛ぶ」代わりに「止まって見える」。
        // 読み込みはこの画面を出しながら走るため、多少飛んでも動き続ける方を採る
        constexpr float kMaxDeltaSeconds = 1.0f / 10.0f;

        // 経過時間の折り返し。走行距離 = speed * time が float の精度を失うほど
        // 大きくならないようにする（この長さの読み込みは現実には起きない）
        constexpr float kTimeWrapSeconds = 1000.0f;

        // 進捗の追従の速さ（1/秒）。大きいほど生の進捗にすぐ追いつく。
        // 速すぎると読み込みのステップがそのまま段差になり、遅すぎると
        // 読み込みが終わってもトロッコが駅へ着かないまま画面が明けてしまう
        constexpr float kProgressFollowRate = 6.0f;
    }

    void TrolleyLoading::OnCreateConstantBuffers()
    {
        UINT paramsSize = (sizeof(TrolleyParams) + 255) & ~255;
        trolleyParamsCB_ = ResourceFactory::CreateBufferResource(graphicsCore_->GetDevice(), paramsSize);
        [[maybe_unused]] HRESULT hr = trolleyParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedTrolleyParams_));
        assert(SUCCEEDED(hr));

        // スプライトを読み込む。sRGB ビューで読まれるのでシェーダー側の Load は既にリニア
        auto& textureManager = TextureManager::GetInstance();
        cartHandle_    = textureManager.Load(kCartTexture).gpuHandle;
        railHandle_    = textureManager.Load(kRailTexture).gpuHandle;
        stationHandle_ = textureManager.Load(kStationTexture).gpuHandle;
        sceneryHandle_ = textureManager.Load(kSceneryTexture).gpuHandle;
        textHandle_    = textureManager.Load(kTextTexture).gpuHandle;

        UpdateConstantBuffer();
    }

    void TrolleyLoading::UpdateConstantBuffer()
    {
        if (!mappedTrolleyParams_) {
            return;
        }
        mappedTrolleyParams_->bobSpeed    = cvBobSpeed.Get();
        mappedTrolleyParams_->railScroll  = cvRailScroll.Get();
        mappedTrolleyParams_->parallax    = cvParallax.Get();
        mappedTrolleyParams_->railY       = cvRailY.Get();
        mappedTrolleyParams_->cartX       = cvCartX.Get();
        mappedTrolleyParams_->cartGoalX   = cvCartGoalX.Get();
        mappedTrolleyParams_->bobAmp      = cvBobAmp.Get();
        mappedTrolleyParams_->tiltDegrees = cvTiltDegrees.Get();
        mappedTrolleyParams_->cartLift    = cvCartLift.Get();
        mappedTrolleyParams_->stationGoal = cvStationGoal.Get();
        mappedTrolleyParams_->stationDrop = cvStationDrop.Get();
        mappedTrolleyParams_->sceneryDrop = cvSceneryDrop.Get();
        mappedTrolleyParams_->scale       = cvScale.Get();
        mappedTrolleyParams_->textScale   = cvTextScale.Get();
        mappedTrolleyParams_->textY       = cvTextY.Get();
        mappedTrolleyParams_->dotInterval = cvDotInterval.Get();
        mappedTrolleyParams_->textGap     = cvTextGap.Get();
        // 表示強度・経過時間・進捗はシーン遷移が制御する実行時値
        mappedTrolleyParams_->screenAlpha = screenAlpha_;
        mappedTrolleyParams_->time        = timeAccumulator_;
        mappedTrolleyParams_->textTime    = textTimeAccumulator_;
        mappedTrolleyParams_->progress    = progress_;
    }

    // デルタタイムに上限を掛けて積算し、進捗を目標へ追従させる
    void TrolleyLoading::PrepareFrame(const PostEffectFrameContext& ctx)
    {
        const float deltaTime = std::min(ctx.deltaTime, kMaxDeltaSeconds);
        timeAccumulator_ = std::fmod(timeAccumulator_ + deltaTime, kTimeWrapSeconds);

        // 文字と点は実測の経過時間で回す。ctx.deltaTime はゲーム時間（エディタで
        // 停止中は 0、スローなら遅い）で、しかも上で 0.1 秒に切り詰めている。
        // それで回すと、重い読み込みの最中ほど「ローディング中…」が止まって見える
        // ―― 動いていることを伝えるための表示なのに逆になる。
        // シーン遷移そのものも Time::UnscaledDeltaTime() で進んでいるので時計が揃う
        textTimeAccumulator_ = std::fmod(
            textTimeAccumulator_ + Time::UnscaledDeltaTime(), kTimeWrapSeconds);

        // シーン構築は 1 フレーム 1 ステップなので、生の進捗は段階的に飛ぶ。
        // そのまま位置にすると、トロッコがワープしたように見える
        const float follow = 1.0f - std::exp(-kProgressFollowRate * deltaTime);
        progress_ += (progressTarget_ - progress_) * follow;

        UpdateConstantBuffer();
    }

    void TrolleyLoading::SetScreenAlpha(float alpha)
    {
        float next = std::clamp(alpha, 0.0f, 1.0f);

        // 表示され始めた瞬間に時間を巻き戻す。遷移のたびにトロッコが同じ姿勢から
        // 走り出すので、前回の遷移の位相を引きずらない
        if (screenAlpha_ <= 0.0f && next > 0.0f) {
            timeAccumulator_     = 0.0f;
            textTimeAccumulator_ = 0.0f;
            // 位置も戻す。前の遷移の到着地点から走り出すと、いきなり駅の前に居る
            progress_       = 0.0f;
            progressTarget_ = 0.0f;
        }

        screenAlpha_ = next;
        UpdateConstantBuffer();
    }

    void TrolleyLoading::SetProgress(float progress)
    {
        // 目標を置くだけ。実際に画面へ出す進捗は PrepareFrame がなめらかに寄せる
        progressTarget_ = std::clamp(progress, 0.0f, 1.0f);
    }

    void TrolleyLoading::SetGaugeAlpha(float /*alpha*/)
    {
        // 進捗は駅の位置で常時見えているので、別建てのゲージは持たない
    }

    void TrolleyLoading::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateConstantBuffer();
        UpdateScreenSizeConstants(width, height);

        auto* cmdList = graphicsCore_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int cartIdx    = GetRootParamIndex("gCart");
        int railIdx    = GetRootParamIndex("gRail");
        int stationIdx = GetRootParamIndex("gStation");
        int sceneryIdx = GetRootParamIndex("gScenery");
        int textIdx    = GetRootParamIndex("gText");
        int outputIdx  = GetRootParamIndex("gOutput");
        int paramsIdx  = GetRootParamIndex("TrolleyParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (cartIdx >= 0)    cmdList->SetComputeRootDescriptorTable(cartIdx, cartHandle_);
        if (railIdx >= 0)    cmdList->SetComputeRootDescriptorTable(railIdx, railHandle_);
        if (stationIdx >= 0) cmdList->SetComputeRootDescriptorTable(stationIdx, stationHandle_);
        if (sceneryIdx >= 0) cmdList->SetComputeRootDescriptorTable(sceneryIdx, sceneryHandle_);
        if (textIdx >= 0)    cmdList->SetComputeRootDescriptorTable(textIdx, textHandle_);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (paramsIdx >= 0)  cmdList->SetComputeRootConstantBufferView(paramsIdx, trolleyParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, GetScreenSizeCbAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void TrolleyLoading::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("TrolleyLoading");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("進捗はトロッコが駅へ近づく距離で表しています");
        ImGui::Text("画面中央の「ローディング中…」は進捗ではなく時間で動きます");
        UI::Separator();

        // 表示強度と進捗はシーン遷移が制御する実行時状態のため、CVar ではなくここで直接編集する
        if (UI::SliderFloat("表示強度（実行時）", screenAlpha_, 0.0f, 1.0f)) {
            UpdateConstantBuffer();
        }
        if (UI::SliderFloat("進捗（実行時）", progressTarget_, 0.0f, 1.0f)) {
            progress_ = progressTarget_;
            UpdateConstantBuffer();
        }

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
            timeAccumulator_     = 0.0f;
            textTimeAccumulator_ = 0.0f;
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* TrolleyLoading::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}

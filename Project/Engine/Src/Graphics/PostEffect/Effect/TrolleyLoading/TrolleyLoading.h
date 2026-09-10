#pragma once

#include "../PostEffectComputeBase.h"
#include "../ILoadingScreenEffect.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief トロッコが走るローディング画面（CS方式）
    /// @details 暗転した画面の下部を、猿を乗せたトロッコが左から右へ走っていく。
    ///          読み込みの進捗は「トロッコが駅までどれだけ近づいたか」で表す。
    ///          動くのは主にトロッコで、駅は右外から少し寄ってくるだけ（トロッコの
    ///          1/4 ほど）。逆にすると駅の方が歩いて来るように見えて違和感が出る。
    ///          進捗が止まってもトロッコはその場で跳ね続けるので画面は固まらない
    ///          ―― シーン構築が 1 フレーム 1 ステップで、所要時間が読めないため。
    ///          画面中央には「ローディング中…」を出す。点は 0→1→2→3 個と増えて戻り、
    ///          文字は同じ周期でゆっくり明滅する（どちらも進捗ではなく時間で回る）。
    /// @note 絵は Assets/Textures/loading_*.png（.obj から焼いたスプライト）と
    ///       loading_text.png（ドット絵フォントを焼いた「ローディング中」）。
    ///       手続き的に描くのは「ローディング中…」の点だけ。
    ///       レイアウト値は CVar（"r.TrolleyLoading.*"）が保持する。
    class TrolleyLoading : public PostEffectComputeBase, public ILoadingScreenEffect {
    public:
        /// @brief パラメータ構造体（GPU 定数バッファのレイアウト）
        /// @note 値の意味は TrolleyLoading.CS.hlsl 側と 1 対 1。位置と大きさは
        ///       すべて「縦 1080 基準のピクセル」で、実解像度へはシェーダーが拡大する
        struct TrolleyParams {
            float screenAlpha = 0.0f;   // 表示強度（実行時値）
            float time        = 0.0f;   // 経過時間（実行時値）
            float progress    = 0.0f;   // 読み込みの進捗（実行時値）
            float bobSpeed    = 336.0f; // 跳ねる周期を決める速さ（px/秒）

            float parallax    = 0.32f;  // 奥の景色の速度比（railScroll に対して）
            float railY       = 0.87f;  // レール上端（画面高さに対する比率）
            float cartX       = -0.14f; // 進捗 0 のトロッコ左端（画面幅に対する比率）
            float bobAmp      = 4.0f;   // 上下の揺れ幅

            float tiltDegrees = 1.6f;   // 前後の傾き
            float cartLift    = 0.0f;   // レール上端から車体下端までの距離（0 でレールに載る）
            float stationGoal = 260.0f; // 進捗 1.0 で駅が来る位置（トロッコ到着位置からの距離）
            float stationDrop = 8.0f;   // レール上端から駅の下端までの距離

            float sceneryDrop = 4.0f;   // レール上端から景色の下端までの距離
            float scale       = 0.72f;  // 全体の拡大率（上の距離とスプライトへ一括で掛かる）
            float cartGoalX   = 0.73f;  // 進捗 1.0 のトロッコ左端（画面幅に対する比率）
            float railScroll  = 0.0f;   // レールと景色が流れる速さ（0 で世界に固定）

            float textTime    = 0.0f;   // 文字と点を回す経過時間（実行時値。実測のまま）
            float textScale   = 1.0f;   // 「ローディング中」の拡大率（1.0 で焼いたままの大きさ）
            float textY       = 0.5f;   // 文字列の中心の高さ（画面高さに対する比率）
            float dotInterval = 0.35f;  // 点が 1 つ増える間隔（秒）

            float textGap     = 21.0f;  // 文字列の右端から最初の点までの距離
            // HLSL 側の cbuffer は textGap で終わるが、cbuffer 全体は 16B 単位へ
            // 切り上げられて 96B になる。C++ 側が 84B のままだとシェーダーが読む
            // 領域を構造体が覆えないので、末尾を float[3] で埋める
            float padding[3]  = {};
        };

        static constexpr Cb::Field kTrolleyParamsFields[] = {
            CB_FIELD(TrolleyParams, screenAlpha), CB_FIELD(TrolleyParams, time),
            CB_FIELD(TrolleyParams, progress),    CB_FIELD(TrolleyParams, bobSpeed),
            CB_FIELD(TrolleyParams, parallax),    CB_FIELD(TrolleyParams, railY),
            CB_FIELD(TrolleyParams, cartX),       CB_FIELD(TrolleyParams, bobAmp),
            CB_FIELD(TrolleyParams, tiltDegrees), CB_FIELD(TrolleyParams, cartLift),
            CB_FIELD(TrolleyParams, stationGoal), CB_FIELD(TrolleyParams, stationDrop),
            CB_FIELD(TrolleyParams, sceneryDrop), CB_FIELD(TrolleyParams, scale),
            CB_FIELD(TrolleyParams, cartGoalX),   CB_FIELD(TrolleyParams, railScroll),
            CB_FIELD(TrolleyParams, textTime),    CB_FIELD(TrolleyParams, textScale),
            CB_FIELD(TrolleyParams, textY),       CB_FIELD(TrolleyParams, dotInterval),
            CB_FIELD(TrolleyParams, textGap),     CB_FIELD(TrolleyParams, padding),
        };
        CB_VERIFY_LAYOUT(TrolleyParams, kTrolleyParamsFields);
        CB_BIND_HLSL(TrolleyParams, kTrolleyParamsFields, "TrolleyParams");

    public:
        TrolleyLoading() = default;
        ~TrolleyLoading() = default;

        /// @brief CSエフェクト実行
        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief 更新処理（経過時間の積算）
        void PrepareFrame(const PostEffectFrameContext& ctx) override;

        /// @brief ImGuiでパラメータを調整
        void DrawImGui() override;

        // ---- ILoadingScreenEffect ----
        void SetScreenAlpha(float alpha) override;
        void SetProgress(float progress) override;

        /// @brief 進捗ゲージの表示強度（この画面では使わない）
        /// @details 進捗はトロッコと駅の距離で常時見えているので、別建てのゲージを
        ///          出す必要が無い。呼ばれても何もしないのが正しい挙動
        void SetGaugeAlpha(float alpha) override;

        void SetLoadingEnabled(bool enabled) override { SetEnabled(enabled); }

    protected:
        /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

        std::string  GetEffectName()        const override { return "TrolleyLoading"; }
        std::wstring GetComputeShaderPath() const override { return L"TrolleyLoading.CS.hlsl"; }

        /// @brief 定数バッファ生成とスプライトの読み込み
        void OnCreateConstantBuffers() override;

    private:
        void UpdateConstantBuffer();

    private:
        Microsoft::WRL::ComPtr<ID3D12Resource> trolleyParamsCB_;
        TrolleyParams* mappedTrolleyParams_ = nullptr;

        D3D12_GPU_DESCRIPTOR_HANDLE cartHandle_    = {};
        D3D12_GPU_DESCRIPTOR_HANDLE railHandle_    = {};
        D3D12_GPU_DESCRIPTOR_HANDLE stationHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneryHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE textHandle_    = {};

        // 実行時状態（保存対象ではない）
        float screenAlpha_     = 0.0f;
        float timeAccumulator_ = 0.0f;
        float textTimeAccumulator_ = 0.0f;  ///< 文字と点用。コマ落ちを切り詰めずに実測を積む
        float progress_        = 0.0f;  ///< 画面に出している進捗（下の目標へ追従する）
        float progressTarget_  = 0.0f;  ///< シーン遷移から渡された生の進捗
    };
}

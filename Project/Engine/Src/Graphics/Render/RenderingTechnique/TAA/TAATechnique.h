#pragma once
#include "../RenderingTechniqueBase.h"
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief TAA (Temporal Anti-Aliasing) レンダリング技術
    /// @details ジッタ付きで描画された SceneColor を、モーションベクターで再投影した
    ///          前フレームの結果と蓄積し、実効サンプル数を増やす。
    ///
    ///          履歴は RenderTargetNames::TAAHistoryA / TAAHistoryB の ping-pong で保持する。
    ///          どちらが今フレームの書き込み先かは frameNumber の偶奇のみから決まるため、
    ///          RenderPipeline 側の論理リソース登録と必ず一致する（GetWriteHistoryIndex）。
    class TAATechnique : public RenderingTechniqueBase {
    public:
        /// @brief シェーダー側 cbuffer TAAParams と一致させること
        struct TAAParams {
            float screenSize[2] = { 1280.0f, 720.0f };
            float jitterDelta[2] = { 0.0f, 0.0f }; ///< 現フレーム - 前フレームのジッタ（NDC）
            float blendAlpha = 0.1f;               ///< 現フレームの寄与率（小さいほど滑らかで残像寄り）
            float clampScale = 1.0f;               ///< 近傍 AABB の拡張率
            float disableHistory = 1.0f;           ///< 1.0 で履歴無効（初回フレーム・リサイズ直後）
            /// @brief 履歴と現フレームが食い違う画素で使う寄与率の上限
            /// @details 水面のように「カメラが止まっていても表面自体が毎フレーム変わる」
            ///          サーフェスは、モーションベクターが完璧でも履歴が本質的に古い。
            ///          固定の blendAlpha=0.1（履歴90%）だと泡のような高周波が
            ///          時間平均されて溶ける。食い違いの大きさで blendAlpha 〜 これの間を
            ///          補間し、静止した不透明面のAA品質は保ったまま動く面だけ現フレーム寄りにする。
            float blendAlphaMax = 0.9f;
        };

    public:
        TAATechnique() = default;
        ~TAATechnique() = default;

        void Initialize(DirectXCommon* dxCommon) override;
        void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
        void OnResize(uint32_t width, uint32_t height) override;
        void DrawImGui() override;

        /// @brief 今フレームの書き込み先履歴インデックスを取得する
        /// @details RenderPipeline の論理リソース登録と Execute の出力先を一致させるための唯一の基準。
        ///          frameNumber のみに依存する純粋関数にしてあるので、呼ぶ順序に影響されない。
        /// @param frameNumber フレーム通し番号
        /// @return 0 = TAAHistoryA / 1 = TAAHistoryB
        static uint32_t GetWriteHistoryIndex(uint64_t frameNumber) { return static_cast<uint32_t>(frameNumber & 1ull); }

        /// @brief 履歴インデックスに対応するレンダーターゲット名を取得する
        /// @param index 0 または 1
        /// @return レンダーターゲット名
        static const char* GetHistoryTargetName(uint32_t index);

        /// @brief 今フレームのジッタを通知し、シェーダーへ渡す差分を更新する
        /// @details モーションベクターはジッタ付きのクリップ座標から作られるため、
        ///          「現フレームのジッタ - 前フレームのジッタ」を引かないと
        ///          静止していても毎フレーム再投影がぶれる。その差分をここで作る。
        ///          同一フレーム内で複数回呼ばれても前回値は進めない。
        /// @param ndcX 今フレームのジッタ X（NDC）
        /// @param ndcY 今フレームのジッタ Y（NDC）
        /// @param frameNumber フレーム通し番号
        void SetJitter(float ndcX, float ndcY, uint64_t frameNumber);

        /// @brief 次フレームの履歴を破棄する（シーン切り替え・カメラ瞬間移動時）
        void InvalidateHistory() { historyValid_ = false; }

        const TAAParams& GetParams() const { return params_; }
        void SetParams(const TAAParams& params) { params_ = params; }

    protected:
    /// @brief 有効/無効は CVar "r.TAA.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

        std::string GetTechniqueName() const override { return "TAA"; }
        const std::wstring& GetPixelShaderPath() const override;

    private:
        TAAParams params_;
        // jitterDelta が毎フレーム変わるため、フレームオーバーラップ対応のリングで運ぶ
        FrameRingConstantBuffer cbRing_;

        bool historyValid_ = false;        ///< 有効な履歴を持っているか
        uint64_t lastExecutedFrame_ = 0;   ///< 直前に Execute したフレーム番号（連続性の判定用）

        bool jitterInitialized_ = false;   ///< 一度でもジッタを受け取ったか
        uint64_t lastJitterFrame_ = 0;     ///< 直前にジッタを受け取ったフレーム番号
        float prevJitterX_ = 0.0f;         ///< 前フレームのジッタ（差分計算用）
        float prevJitterY_ = 0.0f;
    };
}

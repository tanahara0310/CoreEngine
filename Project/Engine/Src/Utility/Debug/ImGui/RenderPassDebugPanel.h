#pragma once

#ifdef USE_IMGUI

#include <d3d12.h>
#include <imgui.h>
#include <array>
#include <string>

namespace CoreEngine
{
    class DirectXCommon;
    class GBufferManager;
    class RenderTargetManager;
    class RenderDomainContext;

    /// @brief レンダーパスデバッグパネル
    /// @details GBuffer 各ターゲット・SSAO・ライティング結果など
    ///          各パスの中間バッファをサムネイル表示する ImGui パネル。
    ///          "EngineDebug > RenderPass" タブとして RegisterEnginePanel から登録して使う。
    class RenderPassDebugPanel
    {
    public:
        RenderPassDebugPanel() = default;
        ~RenderPassDebugPanel() = default;

        /// @brief 初期化
        void Initialize(DirectXCommon* dxCommon);

        /// @brief RenderTargetManager を設定（SSAO / Offscreen ターゲット取得用）
        void SetRenderTargetManager(RenderTargetManager* rtm) { renderTargetManager_ = rtm; }

        /// @brief RenderDomainContext を設定（GBufferManager 取得用）
        void SetRenderDomainContext(RenderDomainContext* rdc) { renderDomainContext_ = rdc; }

        /// @brief ImGui コンテンツを描画（RegisterEnginePanel ラムダから呼ぶ）
        void Draw();

    private:
        // ---- 定数 ----
        static constexpr float kThumbW = 240.0f;
        static constexpr float kThumbH = 135.0f;   // 16:9 比率
        static constexpr float kThumbLarge = 480.0f;

        // ---- 表示用エントリー ----
        struct BufferEntry {
            const char* label = "";
            const char* tooltip = "";
            D3D12_GPU_DESCRIPTOR_HANDLE  srv = {};
            bool valid = false;
        };

        // ---- ヘルパー ----
        /// @brief サムネイル1枚を描画し、クリックで拡大ポップアップを表示する
        void DrawThumbnail(const BufferEntry& entry, float thumbW, float thumbH);

        /// @brief セクションヘッダー（太字区切り）
        static void SectionHeader(const char* label);

        // ---- 状態 ----
        DirectXCommon* dxCommon_ = nullptr;
        RenderDomainContext* renderDomainContext_ = nullptr;
        RenderTargetManager* renderTargetManager_ = nullptr;
        std::string enlargedTitle_;                        ///< 拡大ポップアップのウィンドウタイトル
        D3D12_GPU_DESCRIPTOR_HANDLE enlargedSrv_ = {};     ///< 拡大表示中の SRV
    };

} // namespace CoreEngine

#endif // USE_IMGUI

#pragma once

#ifdef CORE_EDITOR

#include <d3d12.h>
#include <imgui.h>
#include <wrl/client.h>

namespace CoreEngine
{
    class Camera;
    class GraphicsCore;
    class SceneDepth;
}

namespace CoreEngine::Editor
{
    /// @brief シーンビューへ重ねる線を、シーンの深度と比べて、物に隠れた所だけ薄く描く
    /// @details ImGui の描画の途中で専用の PSO へ切り替え、頂点の uv に線の深度（x）と隠れた所の濃さ（y）を入れて描く。
    ///          シーンビューがメインのウィンドウの外にあるときは深度を読まず、隠れた所も同じ濃さで描く。
    class SceneOverlayOcclusion
    {
    public:
        static SceneOverlayOcclusion& Get();

        /// @brief ルートシグネチャと PSO を作る（ImGui を初期化した後に呼ぶ）
        /// @param sceneDepth シーンビューに映すシーンの深度
        void Initialize(GraphicsCore& graphics, SceneDepth* sceneDepth);

        /// @brief GPU のオブジェクトを放す
        void Finalize();

        /// @brief drawList へ、ここから先の線を深度と比べて描くコマンドを積む
        /// @param imagePosition シーンビューの画像の左上（ImGui の画面座標）
        /// @param imageSize シーンビューの画像の大きさ
        /// @return 積んだときだけ true（End と組にする）
        bool Begin(ImDrawList& drawList, const ImVec2& imagePosition, const ImVec2& imageSize, const Camera& camera);

        /// @brief ImGui の描き方へ戻すコマンドを積む
        void End(ImDrawList& drawList);

    private:
        /// @brief ImGui の描画の途中で呼ばれ、PSO と定数を差し替える
        static void Setup(const ImDrawList* drawList, const ImDrawCmd* command);

        GraphicsCore* graphics_ = nullptr;
        SceneDepth* sceneDepth_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPipeline_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> plainPipeline_;
    };
}

#endif // CORE_EDITOR

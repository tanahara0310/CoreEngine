#pragma once

namespace CoreEngine
{
    class RenderPipeline;
    class HiZOcclusionSystem;

    /// @brief エンジン標準のレンダーパス構成を RenderPipeline へ積むビルダー
    /// @details パスの具象型を知るのはこのクラス（正確には .cpp）だけに閉じている。
    ///          登録リスト自体は残しているが、置き場所を EngineSystem から
    ///          パスを所有するこのモジュールへ 1 段下げた。EngineSystem.cpp に置くと
    ///          パスを 1 つ足すたびに EngineSystem.h を include する全ファイルが
    ///          再コンパイル対象になるため（依存グラフの頂点に具象型の列挙を置かない）。
    /// @note ゲーム側の追加パスは RenderPipeline::AddPass で任意フェーズへ挿せる。
    ///       ここを編集する必要はない（RenderPassPhase の説明を参照）。
    class DefaultRenderPipelineBuilder
    {
    public:
        /// @brief 標準構成のパス群を pipeline へ登録する
        /// @param pipeline    登録先。所有権は呼び出し側（EngineSystem）に残る
        /// @param hiZOcclusion Hi-Z オクルージョンカリングシステム。
        ///                     HiZOcclusionPass が非所有ポインタとして保持するため、
        ///                     pipeline より長く生存させること
        static void Build(RenderPipeline& pipeline, HiZOcclusionSystem* hiZOcclusion);
    };
}

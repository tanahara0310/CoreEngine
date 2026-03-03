#pragma once

#ifdef _DEBUG

namespace CoreEngine {

    class Model;

    /// @brief マテリアルデバッグUI - ImGuiを使用したマテリアルパラメータ編集インターフェース
    class MaterialDebugUI {
    public:
        /// @brief マテリアルパラメータのImGuiウィジェットを描画
        /// @param model モデルへのポインタ
        /// @return パラメータに変更があった場合 true
        bool Draw(Model* model);
    };

} // namespace CoreEngine

#endif

#pragma once

#ifdef USE_IMGUI

namespace CoreEngine {

    class Model;

    /// @brief マテリアルデバッグUI - ImGuiを使用したマテリアルパラメータ編集インターフェース
    class MaterialDebugUI {
    public:
        /// @brief マテリアルパラメータのImGuiウィジェットを描画
        /// @param model モデルへのポインタ（マルチマテリアルの場合はスロット選択コンボを表示）
        /// @return パラメータに変更があった場合 true
        bool Draw(Model* model);

    private:
        /// 編集対象のマテリアルスロットインデックス
        int selectedMaterial_ = 0;
    };

} // namespace CoreEngine

#endif

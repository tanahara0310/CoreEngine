#pragma once

#ifdef CORE_EDITOR

#include <string>
#include <unordered_set>
#include <vector>

namespace CoreEngine
{
    struct Skeleton;
}

namespace CoreEngine::Editor
{
    struct AnimatorDocument;
    struct AnimatorLiveView;

    /// @brief アニメーターの窓のレイヤー（左の表と、選んだレイヤーの名前・重み・マスクの骨の木）
    /// @details 表で選んだレイヤーの状態と遷移をキャンバスが出す。
    ///          再生中は、動いているオブジェクトの同じ名前のレイヤーの重みを映し、その場で変えられる。
    class AnimatorLayerPanel
    {
    public:
        /// @brief 左の表（選ぶ・足す・右クリックで順を変える・消す）
        void DrawList(AnimatorDocument& document, const AnimatorLiveView& live);

        /// @brief 右の欄（選んだレイヤーの設定）
        void DrawInspector(AnimatorDocument& document, const AnimatorLiveView& live);

    private:
        /// @brief マスクの骨の木と、全部・外すのボタン
        void DrawMask(AnimatorDocument& document, int layerIndex);

        /// @brief 骨 1 本と子を木で並べる（チェックを変えると子もまとめて変える）
        /// @param children 骨ごとの子の番号
        /// @return チェックを変えたら true
        bool DrawBone(const Skeleton& skeleton, const std::vector<std::vector<int>>& children, int joint,
            std::unordered_set<std::string>& enabled);
    };
}

#endif // CORE_EDITOR

#pragma once
#include "Matrix/Matrix4x4.h"
#include "QuaternionTransform.h"
#include <string>
#include <vector>

/// @brief モデルのNode(ノード)情報を表す構造体
/// Nodeは階層構造を持ち、親子関係で変換行列を連結する

namespace CoreEngine
{
struct Node {
    QuaternionTransform transform; //!< このNodeの変換情報（平行移動、回転、拡縮）
    Matrix4x4 localMatrix;     //!< このNodeのローカル変換行列
    std::string name;            //!< Node名
    std::vector<Node> children;      //!< 子Nodeのリスト
};
}

#pragma once

#include "Animation.h"
#include <string>

// 前方宣言（assimp）
struct aiScene;
struct aiNodeAnim;

/// @brief アニメーションファイル読み込み専用クラス
/// Assimpを使用してglTFなどからアニメーションデータを解析

namespace CoreEngine
{
class AnimationLoader {
public:
    /// @brief アニメーションファイルを読み込む
    /// @param directoryPath ディレクトリパス
    /// @param filename ファイル名
  /// @return 読み込んだアニメーションデータ
    static Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename);

private:
    /// @brief Assimpシーンからアニメーションを解析
    /// @param scene Assimpシーン
    /// @param animationIndex アニメーションインデックス（デフォルト0）
    /// @return 解析されたアニメーション
    static Animation ParseAnimation(const ::aiScene* scene, unsigned int animationIndex = 0);

    /// @brief AssimpのNodeAnimationをNodeAnimationに変換
    /// @param aiNodeAnim Assimpのノードアニメーション
    /// @return 変換されたNodeAnimation
    static NodeAnimation ConvertNodeAnimation(const ::aiNodeAnim* aiNodeAnim, double ticksPerSecond);
};
}

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
    /// @param sourceAnimationName ファイル内のアニメーション名（空 = 先頭の 1 本）
    /// @return 読み込んだアニメーションデータ
    /// @note 1 つのファイルに複数のアニメーションが入っている場合があるため、
    ///       名前で選べるようにしている（例: Fox.gltf の Survey / Walk / Run）。
    static Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename,
        const std::string& sourceAnimationName = "");

private:
    /// @brief 名前からアニメーションのインデックスを引く（見つからなければ 0 を返して警告）
    /// @param scene Assimpシーン
    /// @param sourceAnimationName 探すアニメーション名（空 = 先頭）
    static unsigned int FindAnimationIndex(const ::aiScene* scene, const std::string& sourceAnimationName);

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

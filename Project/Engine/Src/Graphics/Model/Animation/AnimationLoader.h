#pragma once

#include "Animation.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// 前方宣言（assimp）
struct aiScene;
struct aiNodeAnim;

namespace CoreEngine
{
/// @brief アニメーションファイル読み込み専用クラス
/// Assimpを使用してglTFなどからアニメーションデータを解析
class AnimationLoader {
public:
    /// @brief ファイルの全アニメーションを読む（座標系は左手系に直す）
    /// @param outAnimations ファイル内の名前とアニメーション（ファイル内の順）
    /// @param outMeshCount ファイル内のメッシュの数
    /// @return ファイルを読めたか（アニメーションが無くても読めれば true）
    static bool LoadAllAnimations(const std::filesystem::path& filePath,
        std::vector<std::pair<std::string, Animation>>& outAnimations, uint32_t& outMeshCount);

    /// @brief ファイル内の名前でアニメーションを選ぶ（空なら先頭。見つからなければ先頭を返して警告する）
    /// @param fileForLog 警告に出すファイルの名前
    /// @return 選んだアニメーション（1 本も無ければ nullptr）
    static const Animation* Select(const std::vector<std::pair<std::string, Animation>>& animations,
        const std::string& sourceAnimationName, const std::string& fileForLog);

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

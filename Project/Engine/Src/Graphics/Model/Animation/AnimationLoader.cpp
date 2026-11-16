#include "pch.h"
#include "AnimationLoader.h"
#include "Utility/Logger/Logger.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>


namespace CoreEngine
{
namespace {
    /// @brief Assimp のベクトルをエンジンの Vector3 に写す
    Vector3 ToEngineValue(const aiVector3D& value) {
        return { value.x, value.y, value.z };
    }

    /// @brief Assimp のクォータニオンをエンジンの Quaternion に写す
    Quaternion ToEngineValue(const aiQuaternion& value) {
        return { value.x, value.y, value.z, value.w };
    }

    /// @brief Assimp のキー列を秒単位のキーフレームにしてカーブの末尾へ足す
    /// @note 値は変換せずに写す（左手系への変換は読み込み時の aiProcess_ConvertToLeftHanded が済ませている）。
    template <typename T, typename AssimpKey>
    void AppendKeys(const AssimpKey* keys, unsigned int keyCount, double ticksPerSecond, AnimationCurve<T>& outCurve) {
        for (uint32_t keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
            const AssimpKey& keyAssimp = keys[keyIndex];
            Keyframe<T> keyframe;

            // 時刻を秒に変換
            keyframe.time = static_cast<float>(keyAssimp.mTime / ticksPerSecond);
            keyframe.value = ToEngineValue(keyAssimp.mValue);

            outCurve.keyframes.push_back(keyframe);
        }
    }
}

bool AnimationLoader::LoadAllAnimations(const std::filesystem::path& filePath,
    std::vector<std::pair<std::string, Animation>>& outAnimations, uint32_t& outMeshCount) {
    // Assimp はパスを UTF-8 とみなす
    const std::string path = Logger::GetInstance().PathToUtf8(filePath);
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.c_str(), aiProcess_ConvertToLeftHanded);
    if (scene == nullptr) {
        Logger::GetInstance().Warnf(LogCategory::Resource,
            "AnimationLoader: {} を読めませんでした（{}）", path, importer.GetErrorString());
        return false;
    }

    outMeshCount = scene->mNumMeshes;
    outAnimations.clear();
    outAnimations.reserve(scene->mNumAnimations);
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        outAnimations.emplace_back(scene->mAnimations[i]->mName.C_Str(), ParseAnimation(scene, i));
    }
    return true;
}

const Animation* AnimationLoader::Select(const std::vector<std::pair<std::string, Animation>>& animations,
    const std::string& sourceAnimationName, const std::string& fileForLog) {
    if (animations.empty()) {
        Logger::GetInstance().Warnf(LogCategory::Resource,
            "AnimationLoader: {} にアニメーションが入っていません", fileForLog);
        return nullptr;
    }
    if (sourceAnimationName.empty()) {
        return &animations.front().second;
    }
    for (const auto& [name, animation] : animations) {
        if (name == sourceAnimationName) {
            return &animation;
        }
    }

    // 見つからないまま先頭を使うと別のアニメーションが黙って再生されるので警告する
    Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Resource,
        "AnimationLoader: {} にアニメーション '{}' が見つかりません。先頭の 1 本を使用します",
        fileForLog, sourceAnimationName);
    return &animations.front().second;
}

Animation AnimationLoader::ParseAnimation(const aiScene* scene, unsigned int animationIndex) {
    Animation animation;

    // 指定されたインデックスのアニメーションを取得
    const aiAnimation* animationAssimp = scene->mAnimations[animationIndex];

    // アニメーションの長さを秒に変換
    animation.duration = static_cast<float>(animationAssimp->mDuration / animationAssimp->mTicksPerSecond);

    // 各ノードのアニメーションをChannelと呼んでいるのでChannelを回してNodeAnimationの情報をとってくる
    for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {
        const aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];

        // NodeAnimationを変換
        NodeAnimation nodeAnimation = ConvertNodeAnimation(nodeAnimationAssimp, animationAssimp->mTicksPerSecond);

        // Node名でマップに登録
        animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()] = nodeAnimation;
    }

    return animation;
}

NodeAnimation AnimationLoader::ConvertNodeAnimation(const aiNodeAnim* aiNodeAnim, double ticksPerSecond) {
    NodeAnimation nodeAnimation;

    AppendKeys(aiNodeAnim->mPositionKeys, aiNodeAnim->mNumPositionKeys, ticksPerSecond, nodeAnimation.translate);
    AppendKeys(aiNodeAnim->mRotationKeys, aiNodeAnim->mNumRotationKeys, ticksPerSecond, nodeAnimation.rotate);
    AppendKeys(aiNodeAnim->mScalingKeys, aiNodeAnim->mNumScalingKeys, ticksPerSecond, nodeAnimation.scale);

    return nodeAnimation;
}
}

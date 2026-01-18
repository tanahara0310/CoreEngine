#include "AnimationLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>


namespace CoreEngine
{
Animation AnimationLoader::LoadAnimationFile(const std::string& directoryPath, const std::string& filename) {
	// ファイルパスを構築
	std::string filePath = directoryPath + "/" + filename;

	// Assimpでシーンを読み込む（モデルロードと同じフラグを使用）
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		filePath.c_str(),
		aiProcess_ConvertToLeftHanded
	);

	// アニメーションがない場合はアサート
	assert(scene != nullptr && scene->mNumAnimations != 0 && "Animation not found in file");

	// 最初のアニメーションだけ採用（複数対応する場合は引数で制御）
	return ParseAnimation(scene, 0);
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

	// === Translate（平行移動）のキーフレーム ===
	for (uint32_t keyIndex = 0; keyIndex < aiNodeAnim->mNumPositionKeys; ++keyIndex) {
		const aiVectorKey& keyAssimp = aiNodeAnim->mPositionKeys[keyIndex];
		KeyframeVector3 keyframe;

		// 時刻を秒に変換
		keyframe.time = static_cast<float>(keyAssimp.mTime / ticksPerSecond);

		// 値を取得（座標変換は不要：モデルロード時にaiProcess_ConvertToLeftHandedで処理済み）
		keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };

		nodeAnimation.translate.keyframes.push_back(keyframe);
	}

	// === Rotate（回転）のキーフレーム ===
	for (uint32_t keyIndex = 0; keyIndex < aiNodeAnim->mNumRotationKeys; ++keyIndex) {
		const aiQuatKey& keyAssimp = aiNodeAnim->mRotationKeys[keyIndex];
		KeyframeQuaternion keyframe;

		// 時刻を秒に変換
		keyframe.time = static_cast<float>(keyAssimp.mTime / ticksPerSecond);

		// 値を取得（座標変換は不要：モデルロード時にaiProcess_ConvertToLeftHandedで処理済み）
		keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z, keyAssimp.mValue.w };

		nodeAnimation.rotate.keyframes.push_back(keyframe);
	}

	// === Scale（スケール）のキーフレーム ===
	for (uint32_t keyIndex = 0; keyIndex < aiNodeAnim->mNumScalingKeys; ++keyIndex) {
		const aiVectorKey& keyAssimp = aiNodeAnim->mScalingKeys[keyIndex];
		KeyframeVector3 keyframe;

		// 時刻を秒に変換
		keyframe.time = static_cast<float>(keyAssimp.mTime / ticksPerSecond);

		// スケールはそのまま
		keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };

		nodeAnimation.scale.keyframes.push_back(keyframe);
	}

	return nodeAnimation;
}
}

#pragma once

#include "GameObject/Model/AnimatedModelObject.h"

/// @brief BrainStem モデルオブジェクト（MultiMesh & MultiMaterial の実証用）
/// @details Khronos の glTF サンプルモデル。1 つのメッシュが 59 プリミティブに分かれ、
///          それぞれ別マテリアルを参照する。Assimp は各プリミティブを個別の aiMesh として
///          読むため、ModelData には 59 個のサブメッシュと 59 個のマテリアルが載る。
///          さらにスケルトン＋アニメーション付きなので、
///          「マルチメッシュ・マルチマテリアルのスキニングモデル」の題材として使える。
/// @note 配置（translate / scale）はシーン側で行う。
class BrainStemObject : public CoreEngine::AnimatedModelObject {
public:
    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "BrainStem"; }

protected:
    std::string GetModelPath() const override { return "BrainStem.gltf"; }
    std::string GetAnimationName() const override { return "brainStemAnimation"; }
    // テクスチャは持たないモデル（マテリアルの baseColorFactor のみ）。
    // 空文字を返すと ModelResource が既定の白テクスチャを割り当てる。
};

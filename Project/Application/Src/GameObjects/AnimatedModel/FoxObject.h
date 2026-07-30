#pragma once

#include "GameObject/Model/AnimatedModelObject.h"

/// @brief Fox モデルオブジェクト（アニメーションブレンドの実証用）
///
/// Khronos の glTF サンプルモデル。1 つの glTF に **同一スケルトンで動く
/// アニメーションが 3 本**（Survey / Walk / Run）入っている。
/// `SwitchAnimationWithBlend()` でこの 3 本を補間しながら切り替える。
///
/// @note モデルの頂点は cm 単位（体長 155 / 体高 79）で作られており、
///       ルートノードにスケールが入っていない。シーン側で 0.01 倍すること。
class FoxObject : public CoreEngine::AnimatedModelObject {
public:
    // 切り替えに使う識別名（シーン側からも参照する）
    static constexpr const char* kClipSurvey = "foxSurvey";
    static constexpr const char* kClipWalk = "foxWalk";
    static constexpr const char* kClipRun = "foxRun";

    const char* GetObjectName() const override { return "Fox"; }

protected:
    std::string GetModelPath() const override { return "Fox.gltf"; }
    std::string GetTexturePath() const override { return "Texture.png"; }

    // 初期クリップは Survey（その場での見回し）
    std::string GetAnimationName() const override { return kClipSurvey; }
    std::string GetSourceAnimationName() const override { return "Survey"; }

    /// @brief 残り 2 本（Walk / Run）を同じファイルから追加で読み込む
    std::vector<CoreEngine::AnimationClipDesc> GetAdditionalAnimationClips() const override {
        return {
            { kClipWalk, "Walk", "" },
            { kClipRun,  "Run",  "" },
        };
    }
};

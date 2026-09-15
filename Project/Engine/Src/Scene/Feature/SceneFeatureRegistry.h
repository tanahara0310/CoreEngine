#pragma once

#include "Utility/Macro/UniqueName.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class ISceneFeature;

    /// @brief 名前から Feature を作る表
    /// @details シーンの保存データ（`_scene.json` の `features`）に並べた名前から、`DataScene` が Feature を足す。
    ///          登録は Feature の .cpp のファイルスコープで `SCENE_FEATURE_REGISTER("名前", 作る関数)` と書く。
    namespace SceneFeatureRegistry
    {
        using Creator = std::function<std::unique_ptr<ISceneFeature>()>;

        /// @brief 名前と作る関数を登録する（同じ名前は後から登録したものに置き換わる）
        void Register(const std::string& name, Creator creator);

        /// @brief 名前から Feature を作る
        /// @return 登録が無ければ nullptr
        std::unique_ptr<ISceneFeature> Create(const std::string& name);

        /// @brief 登録済みの名前（名前順）
        std::vector<std::string> GetNames();
    }

    /// @brief `SCENE_FEATURE_REGISTER` が使う自己登録ヘルパ
    struct AutoRegisterSceneFeature
    {
        AutoRegisterSceneFeature(const char* name, SceneFeatureRegistry::Creator creator)
        {
            SceneFeatureRegistry::Register(name, std::move(creator));
        }
    };
}

/// 対応する .cpp のファイルスコープで SCENE_FEATURE_REGISTER("名前", 作る関数) と書くと、
/// シーンの保存データからその名前で Feature を足せるようになる。
#define SCENE_FEATURE_REGISTER(Name, CreatorFunction)                                          \
    namespace {                                                                                \
        const ::CoreEngine::AutoRegisterSceneFeature                                           \
            CORE_UNIQUE_NAME(kAutoRegisterSceneFeature_){ Name, CreatorFunction };             \
    }

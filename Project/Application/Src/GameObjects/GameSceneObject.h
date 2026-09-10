#pragma once

#include "GameObject/GameObject.h"

#include <string>

namespace GameScene
{
    /// @brief GameScene 内のコンポーネント設定をオブジェクト単位で保存するアプリ側クラス。
    /// @details エンジンの GameObject / IComponent を変更せず、既存の仮想シリアライズ口を使う。
    class GameSceneObject final : public CoreEngine::GameObject
    {
    public:
        explicit GameSceneObject(const std::string& name) { SetName(name); }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;
    };
}

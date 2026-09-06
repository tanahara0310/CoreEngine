#pragma once

#include "ISceneFeature.h"
#include <memory>
#include <vector>

namespace CoreEngine
{
    /// @brief 既定 Feature 1 件分の生成結果
    struct DefaultSceneFeature {
        std::unique_ptr<ISceneFeature> feature;
        int priority = 0;
    };

    /// @brief どのシーンにも入る既定 Feature 一式を生成する（並び順 = 登録順）
    /// @return 生成済みの Feature 群。呼び出し側が AddFeature() へ流し込む
    /// @details 既定 Feature の顔ぶれを BaseScene から切り離すための入口。
    ///          エンジン機能を全シーンへ載せたくなったら BaseScene ではなく
    ///          この関数へ 1 行足すこと（BaseScene は Feature の型を知らなくてよい）。
    /// @note 同 priority 内はこの並び順がそのまま実行順になる。
    std::vector<DefaultSceneFeature> CreateDefaultSceneFeatures();
}

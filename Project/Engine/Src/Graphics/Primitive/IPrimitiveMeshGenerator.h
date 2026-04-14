#pragma once

#include "Graphics/Model/ModelData.h"

namespace CoreEngine
{
    /// @brief プリミティブメッシュ生成のインターフェース
    /// @note 新しい形状を追加するには、このインターフェースを実装した具象クラスを作成する。
    class IPrimitiveMeshGenerator {
    public:
        virtual ~IPrimitiveMeshGenerator() = default;

        /// @brief メッシュデータ（頂点・インデックス・サブメッシュ）を生成する
        /// @return 生成されたモデルデータ
        virtual ModelData Generate() const = 0;

        /// @brief キャッシュ用の一意キーを返す
        /// @return 形状パラメータを含むキー文字列
        virtual std::string GetCacheKey() const = 0;
    };
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Graphics/Model/ModelData.h"

namespace CoreEngine
{
    /// @brief モデルのファイル（glTF・OBJ・FBX など）を .cmesh へ変換する
    /// @details Assimp で読み、LOD を作って GPU 向けに並べ替え、ファイル内のアニメーションも一緒に書く。
    ///          開発中の派生データと書き出しのクックの両方が使う。GPU もキャッシュも触らない。
    namespace MeshImporter
    {
        /// @brief 変換の中身を変えたら上げる版（派生データのキーに入り、古い .cmesh を作り直させる）
        inline constexpr uint32_t kVersion = 1;

        /// @brief 変換の設定の文字列（派生データのキーに入る）
        std::string Settings();

        /// @brief モデルのファイルを読み、.cmesh として書く
        /// @return 書けたら true（メッシュもアニメーションも無いファイルは false）
        bool Import(const std::filesystem::path& sourcePath, const std::filesystem::path& cmeshPath);

        /// @brief サブメッシュごとに lods[0] を原型の範囲で埋め、LOD は 1 段だけにする
        void InitLod0(ModelData& data);

        /// @brief サブメッシュごとに簡略化インデックス（LOD1・LOD2）を作り、indices の末尾へ足す
        /// @details 頂点バッファは共有する。ハイポリのモデルだけが対象（小物は原型のまま）。
        ///          lods[0] は InitLod0 と同じく原型の範囲で埋める。
        void GenerateLods(ModelData& data);

        /// @brief meshoptimizer で GPU 向けにジオメトリを並べ替える
        /// @details 頂点キャッシュとオーバードローの最適化を全 LOD の範囲へ掛ける。
        ///          静的モデル（skinClusterData が空）だけ、頂点フェッチの最適化で頂点を並べ替える
        ///          （スキンモデルは influence バッファが頂点の番号を指すので並べ替えない）。
        void OptimizeForGpu(ModelData& data);
    }
}

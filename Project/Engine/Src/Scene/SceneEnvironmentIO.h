#pragma once

#include <cstdint>
#include <string>

/// @file
/// @brief シーンが持つ見た目（環境とポストエフェクト）の保存と復元

namespace CoreEngine
{
    class EngineSystem;

    /// @brief シーンフォルダ内の見た目のファイルを読み書きする
    ///
    /// @details
    /// 空・大気・雲・霧・時刻とポストエフェクトはシーンの画そのものなので、
    /// プロジェクト共通の CVars.json ではなくシーンのフォルダへ置く。
    /// 実行時の値は CVar 1 本のままで、変わるのは保存先だけ。
    /// シーンを開くときに一度コード既定へ戻してから当てるので、前のシーンの画が残らない。
    /// どれをシーンが持つかは `CVarScopes::SceneOwnedPrefixes()` が決める。
    class SceneEnvironmentIO {
    public:
        /// @brief シーン名から見た目のファイルのパスを作る
        /// @details オブジェクトの保存先（Assets/Scenes/{scene}/）と同じ場所に置く。
        static std::string GetFilePath(const std::string& sceneName);

        /// @brief シーン名から雲の配置ペイントのパスを作る
        static std::string GetCloudPaintFilePath(const std::string& sceneName);

        /// @brief シーンが持つ設定をすべてコード既定へ戻す
        /// @param engine 雲の配置ペイントを引くために使う（nullptr 可）
        /// @note シーンを開く直前に呼ぶ。保存が無いシーンは「何も上書きしていない画」になる。
        static void ResetToDefaults(EngineSystem* engine);

        /// @brief シーンの見た目を読み込んで当てる
        /// @return ファイルが無い / 読めない場合は false（CVar は変更されない）
        static bool Load(const std::string& sceneName, EngineSystem* engine);

        /// @brief 今の見た目をシーンへ書く
        /// @details コード既定のままの項目は書かない。ファイルが「このシーンの変更点一覧」になる。
        static bool Save(const std::string& sceneName);

        /// @brief シーンが持つ CVar の変更通番の合計
        /// @details 「シーンが持つ値のどれかが変わったか」を毎フレーム安く見るために使う。
        static uint64_t GetChangeRevision();
    };
}

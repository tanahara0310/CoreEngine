#pragma once

#include <cstdint>
#include <string>

/// @file
/// @brief シーンが持つ環境（空・大気・雲・霧・時刻）の保存と復元

namespace CoreEngine
{
    class EngineSystem;

    /// @brief シーンフォルダ内の環境ファイルを読み書きする
    ///
    /// @details
    /// 環境はシーンの画そのものなので、プロジェクト共通の CVars.json ではなく
    /// シーンのフォルダへ置く。実行時の値は CVar 1 本のままで、変わるのは保存先だけ。
    /// シーンを開くときに一度コード既定へ戻してから当てるので、前のシーンの空が残らない。
    class SceneEnvironmentIO {
    public:
        /// @brief シーン名から環境ファイルのパスを作る
        /// @details オブジェクトの保存先（Assets/Scenes/{scene}/）と同じ場所に置く。
        static std::string GetFilePath(const std::string& sceneName);

        /// @brief シーン名から雲の配置ペイントのパスを作る
        static std::string GetCloudPaintFilePath(const std::string& sceneName);

        /// @brief シーンが持つ設定をすべてコード既定へ戻す
        /// @param engine 雲の配置ペイントを引くために使う（nullptr 可）
        /// @note シーンを開く直前に呼ぶ。保存が無いシーンは「何も上書きしていない画」になる。
        static void ResetToDefaults(EngineSystem* engine);

        /// @brief シーンの環境を読み込んで当てる
        /// @return ファイルが無い / 読めない場合は false（CVar は変更されない）
        static bool Load(const std::string& sceneName, EngineSystem* engine);

        /// @brief 今の環境をシーンへ書く
        /// @details コード既定のままの項目は書かない。ファイルが「このシーンの変更点一覧」になる。
        static bool Save(const std::string& sceneName);

        /// @brief シーンが持つ CVar の変更通番の合計
        /// @details 「環境のどれかが変わったか」を毎フレーム安く見るために使う。
        static uint64_t GetChangeRevision();
    };
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CoreEngine
{
    class ShaderMaterial;

    /// @brief 読み込んだマテリアル（.mat）を、パスごとに 1 つずつ共有する
    /// @details エディタでは .mat とシェーダのファイルの更新を見て、読み直しと作り直しをする。
    ///          再生を止めたら、再生中にスクリプトが変えた値を捨ててファイルの中身へ戻す。
    class ShaderMaterialLibrary
    {
    public:
        static ShaderMaterialLibrary& GetInstance();

        /// @brief マテリアルを引く（初めてなら読む）
        /// @param pathOrName `Application/Assets/…` のパスかファイル名
        /// @return 見つからない・読めなければ nullptr
        std::shared_ptr<ShaderMaterial> Get(std::string_view pathOrName);

        /// @brief フレームの初めに呼ぶ（再生の停止とファイルの更新を見る）
        void BeginFrame();

        /// @brief シェーダのファイルが変わるたびに増える番号（パイプラインの作り直しに使う）
        uint64_t GetShaderRevision() const { return shaderRevision_; }

        /// @brief シェーダを作り直させる（シェーダのファイルを書き換えたとき）
        void InvalidateShaders() { ++shaderRevision_; }

        /// @brief 読み込んだマテリアルを全部捨てる（プロジェクトを開き直したとき）
        void Clear();

    private:
        ShaderMaterialLibrary() = default;

        /// @brief 読んだ .mat とシェーダのファイルが書き換わっていないか見る
        void CheckFileChanges();

        std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<ShaderMaterial>> materials_;
        /// シェーダのフルパス → 見たときの更新時刻
        std::unordered_map<std::string, std::filesystem::file_time_type> shaderWriteTimes_;
        uint64_t shaderRevision_ = 1;
        bool wasInPlayMode_ = false;
        uint64_t framesUntilCheck_ = 0;
    };
}

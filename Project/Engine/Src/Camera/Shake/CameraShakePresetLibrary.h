#pragma once

#include "Camera/Shake/CameraShakeTypes.h"

#include <string>
#include <vector>

/// @file
/// @brief 名前付きシェイクプリセットの保管庫（エディタで編集し、シーケンスから名前で呼ぶ）

namespace CoreEngine
{
    /// @brief シェイクプリセット 1 件
    struct CameraShakePreset {
        std::string name;
        CameraShakeParams params{};
    };

    /// @brief 名前 → 揺れの定義を持つ、保存可能なプリセット集
    ///
    /// @details
    /// シーケンスのイベントトラックは揺れの中身ではなく**名前**だけを持つ。実体をここへ
    /// 集約することで、揺れの調整はエディタ側で完結し、シーケンスを触らずに全カットの
    /// 揺れを差し替えられる。
    ///
    /// 初期状態では CameraShakePresets の組み込みプリセットが入っている。保存すると
    /// json が書き出され、以後はそちらが読まれる。
    ///
    /// @note メインスレッド専用。
    class CameraShakePresetLibrary {
    public:
        /// @brief 既定の保存先ディレクトリ
        static constexpr const char* kDirectory = "Application/Assets/Presets/CameraShakes/";

        /// @brief 既定のファイル名
        static constexpr const char* kFileName = "presets.json";

        CameraShakePresetLibrary();

        /// @brief 名前でプリセットを引く（無ければ nullptr）
        const CameraShakeParams* Find(const std::string& name) const;

        /// @brief 名前でプリセットを引く（編集用。無ければ nullptr）
        CameraShakeParams* FindMutable(const std::string& name);

        /// @brief 追加または上書きする
        void Set(const std::string& name, const CameraShakeParams& params);

        /// @brief 削除する（存在しなければ何もしない）
        void Remove(const std::string& name);

        /// @brief 名前を変える（新しい名前が既にあれば false）
        bool Rename(const std::string& from, const std::string& to);

        /// @brief 全プリセット（登録順）
        const std::vector<CameraShakePreset>& GetAll() const { return presets_; }

        /// @brief 組み込みプリセットで初期化し直す
        void ResetToBuiltIn();

        /// @brief 既定の保存先へ書き出す
        bool Save() const;

        /// @brief 既定の保存先から読み込む
        /// @return ファイルが無い / 読めない場合は false（中身は変わらない）
        bool Load();

        /// @brief 既定の保存先のフルパス
        static std::string GetFilePath();

    private:
        std::vector<CameraShakePreset> presets_;
    };
}

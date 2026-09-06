#pragma once

#include "Camera/Rig/CameraRigLibrary.h"
#include "Camera/Rig/CameraRigRuntime.h"

#include <string>

/// @file
/// @brief カメラリグの静的な入口（どこからでも 1 行で切り替えるため）

namespace CoreEngine
{
    /// @brief 現在のシーンの CameraRigRuntime へ委譲する静的ファサード
    ///
    /// @details
    /// 切り替えたい側が Camera も CameraManager も知らずに済むようにするための入口。
    /// 委譲先は CameraRigFeature が Initialize / PostSceneFinalize で差し替える。
    /// 委譲先が無い間（シーン外・Feature 未登録）はすべて無害な空振りになる。
    ///
    /// リグの実体はエディタで作った .json で、コード側が知っているのは名前だけ。
    /// 追従の距離・角度・視野角をコードに書かずに済ませるための境界がここ。
    ///
    /// @code
    ///     // 通常のゲームカメラ
    ///     CameraRig::Activate("GameCamera");
    ///
    ///     // 戦闘に入ったら引きの構図へ、0.5 秒かけて繋ぐ
    ///     CameraRigActivateOptions options;
    ///     options.blendSeconds = 0.5f;
    ///     CameraRig::Activate("Battle_Wide", options);
    ///
    ///     // カメラをゲーム側の追従へ返す
    ///     CameraRig::Deactivate();
    /// @endcode
    ///
    /// カットシーンの間は CameraSequence がこの上に重なる。リグを止める必要はない。
    /// 重ね順は「リグ → シーケンス → 揺れ」で、DefaultSceneFeatures の登録順が決めている。
    ///
    /// @note メインスレッド専用。
    class CameraRig {
    public:
        /// @brief 名前でリグを動かし始める
        /// @param name 拡張子なしのリグ名（"GameCamera"）
        /// @return 動かし始められたら true（未登録・ファイル無しなら false）
        static bool Activate(const std::string& name, const CameraRigActivateOptions& options = {});

        /// @brief リグを止め、カメラをゲーム側へ返す
        static void Deactivate();

        /// @brief リグがカメラを握っているか
        /// @details 「今カメラを動かしているのは誰か」の判定はこちらを見ること。
        static bool IsActive();

        /// @brief 動作中のリグ名（止まっていれば空）
        static std::string GetActiveName();

        /// @brief リグの読み込みキャッシュ
        /// @details ディレクトリの変更や、エディタで保存し直した後の Reload に使う。
        static CameraRigLibrary& GetLibrary() { return library_; }

        /// @brief 委譲先が居るか（通常は確かめる必要はない）
        static bool IsAvailable() { return activeRuntime_ != nullptr; }

        /// @brief 委譲先のランタイム（細かい制御が要るとき用。無ければ nullptr）
        static CameraRigRuntime* GetActiveRuntime() { return activeRuntime_; }

        /// @brief 委譲先を差し替える
        /// @note CameraRigFeature が呼ぶ。利用側は呼ばないこと。
        static void SetActiveRuntime(CameraRigRuntime* runtime) { activeRuntime_ = runtime; }

    private:
        /// @brief 委譲先（非所有。所有は CameraRigFeature）
        static inline CameraRigRuntime* activeRuntime_ = nullptr;

        /// @brief 読み込みキャッシュ。ランタイムはシーンごとに作り直されるので、こちらが正本
        static inline CameraRigLibrary library_{};
    };
}

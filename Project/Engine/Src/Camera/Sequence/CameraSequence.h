#pragma once

#include "Camera/Sequence/CameraSequenceLibrary.h"
#include "Camera/Sequence/CameraSequencePlayer.h"

#include <string>

/// @file
/// @brief カメラシーケンスの静的な入口（どこからでも 1 行で再生するため）

namespace CoreEngine
{
    /// @brief 現在のシーンの CameraSequencePlayer へ委譲する静的ファサード
    ///
    /// @details
    /// 再生したい側が Camera も CameraManager も知らずに済むようにするための入口。
    /// 委譲先は CameraSequenceFeature が Initialize / PostSceneFinalize で差し替える。
    /// 委譲先が無い間（シーン外・Feature 未登録）はすべて無害な空振りになる。
    ///
    /// シーケンスの実体はエディタで作った .json で、コード側が知っているのは名前だけ。
    /// カット割り・タイミング・構図をコードに書かずに済ませるための境界がここ。
    ///
    /// @code
    ///     CameraSequence::Play("Opening_Station");
    ///
    ///     CameraSequencePlaybackOptions options;
    ///     options.blendInSeconds = 0.4f;   // 今の構図から 0.4 秒かけて繋ぐ
    ///     options.useUnscaledTime = true;  // ヒットストップ中も進める
    ///     CameraSequence::Play("Bridge_Collapse", options);
    /// @endcode
    ///
    /// @note メインスレッド専用。
    class CameraSequence {
    public:
        /// @brief 名前でシーケンスを頭から再生する
        /// @param name 拡張子なしのシーケンス名（"Opening_Station"）
        /// @return 再生を開始できたら true（未登録・ファイル無しなら false）
        static bool Play(const std::string& name, const CameraSequencePlaybackOptions& options = {});

        /// @brief 再生を止め、カメラをゲーム側へ返す
        static void Stop();

        /// @brief 再生ヘッドを止める（構図はその位置で保持される）
        static void Pause();

        /// @brief 一時停止から再開する
        static void Resume();

        /// @brief 再生ヘッドが進んでいるか
        static bool IsPlaying();

        /// @brief シーケンスがカメラを握っているか（一時停止中も含む）
        /// @details 「今カメラを動かしているのは誰か」の判定はこちらを見ること。
        static bool IsActive();

        /// @brief 再生中のシーケンス名（何も再生していなければ空）
        static std::string GetPlayingName();

        /// @brief シーケンスの読み込みキャッシュ
        /// @details ディレクトリの変更や、エディタで保存し直した後の Reload に使う。
        static CameraSequenceLibrary& GetLibrary() { return library_; }

        /// @brief 委譲先が居るか（通常は確かめる必要はない）
        static bool IsAvailable() { return activePlayer_ != nullptr; }

        /// @brief 委譲先の Player（細かい制御が要るとき用。無ければ nullptr）
        static CameraSequencePlayer* GetActivePlayer() { return activePlayer_; }

        /// @brief 委譲先を差し替える
        /// @note CameraSequenceFeature が呼ぶ。利用側は呼ばないこと。
        static void SetActivePlayer(CameraSequencePlayer* player) { activePlayer_ = player; }

    private:
        /// @brief 委譲先（非所有。所有は CameraSequenceFeature）
        static inline CameraSequencePlayer* activePlayer_ = nullptr;

        /// @brief 読み込みキャッシュ。Player はシーンごとに作り直されるので、こちらが正本
        static inline CameraSequenceLibrary library_{};
    };
}

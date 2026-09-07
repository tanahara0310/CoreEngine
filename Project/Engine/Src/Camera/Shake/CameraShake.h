#pragma once

#include "Camera/Shake/CameraShakePresetLibrary.h"
#include "Camera/Shake/CameraShakeTypes.h"

#include <string>

/// @file
/// @brief カメラシェイクの静的な入口（どこからでも 1 行で揺らすため）

namespace CoreEngine
{
    class CameraShaker;

    /// @brief 現在のシーンの CameraShaker へ委譲する静的ファサード
    ///
    /// @details
    /// 揺らしたい側が Camera も CameraManager も知らずに済むようにするための入口。
    /// 委譲先は CameraShakeFeature が Initialize / PostSceneFinalize で差し替える。
    /// 委譲先が無い間（シーン外・Feature 未登録）はすべて無害な空振りになるので、
    /// 呼び出し側で有無を確かめる必要はない。
    ///
    /// もう 1 つの入口として EventBus 版（CameraShakeEvent）がある。使い分けは:
    ///  - こちら … 演出コードが「今すぐこの揺れを出す」と決めているとき
    ///  - Event  … 「敵が死んだ」など事実だけを投げ、揺らすかどうかは受け手に委ねたいとき
    ///
    /// @code
    ///     CameraShake::Play(CameraShakePresets::Hit());
    ///     CameraShake::Play(CameraShakePresets::Explosion(), explosionWorldPosition);
    ///
    ///     const ShakeHandle quake = CameraShake::Play(CameraShakePresets::Earthquake());
    ///     CameraShake::Stop(quake, 1.5f);
    /// @endcode
    ///
    /// @note メインスレッド専用。
    class CameraShake {
    public:
        /// @brief 揺れを 1 件再生する
        /// @return ハンドル（委譲先が無ければ 0）
        static ShakeHandle Play(const CameraShakeParams& params);

        /// @brief 発生源つきで揺れを再生する（爆発・着弾など）
        static ShakeHandle Play(const CameraShakeParams& params, const Vector3& worldOrigin);

        /// @brief trauma を蓄積する（小さいヒットの連続を自然にスタックさせる）
        static void AddTrauma(float amount);

        /// @brief 指定の揺れを止める
        static void Stop(ShakeHandle handle, float fadeOutSeconds = 0.0f);

        /// @brief すべての揺れを止める
        static void StopAll(float fadeOutSeconds = 0.0f);

        /// @brief 全体強度（0 で完全に無効）
        /// @note 画面揺れは 3D 酔いの原因になるため、0 にできる口を設定画面へ出すこと。
        ///       値はシーンをまたいで保持され、委譲先が変わるたびに引き継がれる。
        static void SetGlobalScale(float scale);
        static float GetGlobalScale() { return globalScale_; }

        /// @brief 名前でプリセットを再生する
        /// @param name プリセット名（CameraShake::GetPresetLibrary() に登録されているもの）
        /// @param scale 振幅の倍率（1.0 でそのまま）
        /// @return ハンドル（プリセットが無い / 委譲先が無ければ 0）
        /// @details シーケンスのイベントトラックはこの入口を使う。揺れの中身は
        ///          エディタで編集し、シーケンス側は名前だけを持つ。
        static ShakeHandle PlayPreset(const std::string& name, float scale = 1.0f);

        /// @brief 名前付きプリセットの保管庫
        /// @details Shaker はシーンごとに作り直されるので、こちらが正本。
        static CameraShakePresetLibrary& GetPresetLibrary() { return presetLibrary_; }

        /// @brief 委譲先が居るか（通常は確かめる必要はない）
        static bool IsAvailable() { return activeShaker_ != nullptr; }

        /// @brief 委譲先の CameraShaker（細かい制御が要るとき用。無ければ nullptr）
        static CameraShaker* GetActiveShaker() { return activeShaker_; }

        /// @brief 委譲先を差し替える
        /// @note CameraShakeFeature が呼ぶ。利用側は呼ばないこと。
        static void SetActiveShaker(CameraShaker* shaker);

    private:
        /// @brief 委譲先（非所有。所有は CameraShakeFeature）
        static inline CameraShaker* activeShaker_ = nullptr;

        /// @brief 全体強度。Shaker はシーンごとに作り直されるので、こちらが正本
        static inline float globalScale_ = 1.0f;

        /// @brief プリセット集（組み込みで初期化され、保存すると json が正本になる）
        static inline CameraShakePresetLibrary presetLibrary_{};
    };
}

#pragma once

#include "Camera/Sequence/CameraSequenceTypes.h"
#include "Math/Easing/EasingUtil.h"

#include <functional>

/// @file
/// @brief カメラシーケンスの評価（時刻 → カメラ姿勢）
/// @details 入力はシーケンスと時刻だけ、出力はスナップショットだけの純関数群。
///          エディタのプレビューもランタイムの再生もここを通す。以前は編集モジュールと
///          再生モジュールが同じ評価を別々に持っており、片方を直すと動きが食い違った。

namespace CoreEngine
{
    /// @brief シーケンスに保存するイージング種別（JSON の easingTypeIndex が指す表）
    /// @note 並び順が添字としてファイルに入る。要素の挿入・削除は既存アセットの見た目を変える。
    namespace CameraSequenceEasing
    {
        /// @brief 選択肢の数
        int Count();

        /// @brief 添字に対応するイージング種別（範囲外は Linear）
        EasingUtil::Type TypeAt(int index);

        /// @brief 添字に対応する表示名（範囲外は先頭の名前）
        const char* LabelAt(int index);
    }

    /// @brief 注視対象（LookAtObject）をワールド座標へ解決するための受け口
    /// @details 評価器がシーンを知らずに済ませるための境界。渡さなければ LookAtObject は
    ///          解決できず、そのキーは保存された回転（Euler）へ落ちる。
    struct CameraSequenceAimContext {
        /// @brief オブジェクト名 → ワールド座標。見つからなければ false を返すこと
        std::function<bool(const std::string& name, Vector3& outPosition)> resolveObject;
    };

    /// @brief シーケンスを時刻で評価する
    class CameraSequenceEvaluator {
    public:
        /// @brief 指定時刻のカメラ姿勢を求める（ショット遷移を適用）
        /// @param asset 評価対象のシーケンス
        /// @param time タイムライン時刻（範囲外はクランプ）
        /// @param outSnapshot 評価結果
        /// @param aim 注視対象の解決口（nullptr 可。LookAtObject を使うシーケンスでのみ要る）
        /// @return キーフレームが 1 つも無ければ false
        static bool Evaluate(const CameraSequenceAsset& asset, float time, CameraSnapshot& outSnapshot,
            const CameraSequenceAimContext* aim = nullptr);

        /// @brief ショット遷移を無視し、キーフレーム列だけで評価する
        /// @details ショットの繋ぎを見ずに素の軌道を確認したいとき（プレビューのスクラブなど）に使う。
        static bool EvaluateRaw(const CameraSequenceAsset& asset, float time, CameraSnapshot& outSnapshot,
            const CameraSequenceAimContext* aim = nullptr);

        /// @brief キーの注視先ワールド座標を求める
        /// @return Euler 指定、または対象を解決できなければ false（保存された回転を使うべき）
        static bool ResolveAimTarget(const CameraSequenceKeyframe& key,
            const CameraSequenceAimContext* aim, Vector3& outTarget);

        /// @brief 視点から注視先を向くオイラー角を求める（Camera::LookAt と同じ変換）
        /// @param roll 視線軸まわりの傾き [ラジアン]
        static Vector3 LookRotation(const Vector3& eye, const Vector3& target, float roll);

        /// @brief 2 つのスナップショットを直線で補間する
        /// @param t 0..1 の補間係数
        /// @param easing 適用するイージング
        static CameraSnapshot Interpolate(const CameraSnapshot& from, const CameraSnapshot& to,
            float t, EasingUtil::Type easing);

        /// @brief キーの緩急を解決する（キーが既定指定ならシーケンス側の値を使う）
        static EasingUtil::Type ResolveEasing(const CameraSequenceAsset& asset,
            const CameraSequenceKeyframe& key);

        /// @brief 指定時刻を含む有効なショットの添字を返す
        /// @return 見つからなければ -1
        static int FindShotIndexAt(const CameraSequenceAsset& asset, float time);
    };
}

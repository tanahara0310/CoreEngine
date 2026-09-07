#pragma once

#include "Camera/Rig/CameraRigTypes.h"

#include <functional>
#include <string>
#include <vector>

/// @file
/// @brief カメラリグの評価（シーンの状態 → カメラ姿勢）
/// @details 決め方は 2 段に分かれている。
///          1. EvaluateDesired … 減衰を掛けない「理想の姿勢」を求める。純関数。
///          2. ApplyDamping    … 前フレームの結果から目標へ寄せる。状態を進める。
///          分けてあるのは、理想の姿勢だけならフレームをまたがずに単体で試せるから。
///          エディタのプレビューも、実行時の追従も、まとめ役の Evaluate を通す。

namespace CoreEngine
{
    /// @brief 対象 1 件の、リグが必要とする状態
    struct CameraRigTargetState {
        Vector3 position = { 0.0f, 0.0f, 0.0f };

        /// @brief オイラー角 [ラジアン]（offsetSpace が Target のときに使う）
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };

        /// @brief ワールド速度 [m/s]（SpeedToFov が使う）
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };

        /// @brief velocity が埋まっているか
        /// @details 速度を持たないシーンでも SpeedToFov が破綻しないようにするための印。
        bool hasVelocity = false;
    };

    /// @brief 対象名をシーンから引く受け口
    /// @details 評価器がシーンを知らずに済ませるための境界。渡さなければ Fixed 以外の
    ///          モードは対象を解決できず、評価は失敗する。
    struct CameraRigContext {
        /// @brief オブジェクト名 → 状態。見つからなければ false を返すこと
        std::function<bool(const std::string& name, CameraRigTargetState& outState)> resolveTarget;

        /// @brief 画面のアスペクト比（0 以下なら 16:9 とみなす）
        /// @details 注視先を画面のどこに置くかの計算に要る。
        float aspectRatio = 0.0f;
    };

    /// @brief 減衰を掛ける前の理想の姿勢
    struct CameraRigPose {
        Vector3 position = { 0.0f, 0.0f, 0.0f };

        /// @brief オイラー角 [ラジアン]（Aim が FollowBody のときだけ意味を持つ）
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };

        /// @brief 視野角 [ラジアン]
        float fov = 0.0f;

        /// @brief 注視先ワールド座標
        Vector3 aimPoint = { 0.0f, 0.0f, 0.0f };

        /// @brief aimPoint が有効か（FollowBody では false）
        bool hasAimPoint = false;
    };

    /// @brief リグの継続状態
    /// @details 減衰は前フレームの結果からしか計算できないので、ここに持つ。
    ///          リグを切り替えたら Reset するか、新しい状態を用意すること。
    struct CameraRigState {
        /// @brief 1 度でも評価したか（初回は減衰を掛けず目標へ直接置く）
        bool initialized = false;

        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        float fov = 0.0f;
        Vector3 aimPoint = { 0.0f, 0.0f, 0.0f };

        /// @brief Spring で寄せるときの速度（Exponential では使わない）
        /// @details バネは速度を次のフレームへ持ち越すことで、目標が飛んだ瞬間に
        ///          速度が跳ねないようにする。ここを落とすとカクつきが戻る。
        Vector3 positionVelocity = { 0.0f, 0.0f, 0.0f };
        Vector3 aimVelocity = { 0.0f, 0.0f, 0.0f };
        float fovVelocity = 0.0f;

        /// @brief 次の評価を初回として扱う
        void Reset()
        {
            initialized = false;
            positionVelocity = { 0.0f, 0.0f, 0.0f };
            aimVelocity = { 0.0f, 0.0f, 0.0f };
            fovVelocity = 0.0f;
        }
    };

    /// @brief リグをシーンの状態で評価する
    class CameraRigEvaluator {
    public:
        /// @brief 減衰を掛けた姿勢を求め、状態を 1 フレーム進める
        /// @param asset 評価対象のリグ
        /// @param context 対象の解決口（nullptr なら Fixed 以外は失敗する）
        /// @param deltaTime 前フレームからの経過時間 [秒]
        /// @param state 継続状態（この関数が書き換える）
        /// @param outSnapshot 評価結果
        /// @return 対象を解決できず姿勢が決まらなければ false
        static bool Evaluate(const CameraRigAsset& asset, const CameraRigContext* context,
            float deltaTime, CameraRigState& state, CameraSnapshot& outSnapshot);

        /// @brief 減衰を掛けない理想の姿勢を求める
        /// @return 対象を解決できなければ false
        static bool EvaluateDesired(const CameraRigAsset& asset, const CameraRigContext* context,
            CameraRigPose& outPose);

        /// @brief 理想の姿勢へ向けて状態を 1 フレーム進める
        /// @details state.initialized が false なら減衰を掛けずに目標へ直接置く。
        ///          初回に減衰を掛けると、原点から目標へ滑り込む見苦しい動きになる。
        static void ApplyDamping(const CameraRigAsset& asset, const CameraRigPose& desired,
            float deltaTime, CameraRigState& state);

        /// @brief 臨界減衰のバネで 1 フレーム分寄せる
        /// @param current 今の値（書き換える）
        /// @param velocity 今の速度（書き換える）
        /// @param target 目標値
        /// @param speed 1 秒あたりの追従の速さ（0 以下で目標へ直接置く）
        /// @param deltaTime 経過時間 [秒]
        /// @details 指数減衰と違って速度が連続に変わるので、目標が飛び飛びでも
        ///          画がカクつかない。同じ speed なら寄る速さは指数減衰とほぼ揃う。
        static void SpringDamp(float& current, float& velocity, float target,
            float speed, float deltaTime);

        /// @brief 臨界減衰のバネで 1 フレーム分寄せる（軸ごと）
        static void SpringDamp(Vector3& current, Vector3& velocity, const Vector3& target,
            float speed, float deltaTime);

        /// @brief 指数減衰の係数を求める
        /// @param speed 1 秒あたりの追従の速さ（0 以下で減衰なし）
        /// @param deltaTime 経過時間 [秒]
        /// @return 0..1 の補間係数
        /// @details 1 - exp(-speed * dt)。フレームレートが変わっても見た目の
        ///          追従速度が変わらない形。
        static float DampingFactor(float speed, float deltaTime);

        /// @brief 視点から注視先を向くオイラー角を求める（Camera::LookAt と同じ変換）
        static Vector3 LookRotation(const Vector3& eye, const Vector3& target, float roll);

        /// @brief 注視先が画面の指定位置に来るよう回転をずらす
        /// @param rotation 注視先を画面中央に置く回転 [ラジアン]
        /// @param screenX 画面内の横位置（0 = 左端 / 0.5 = 中央 / 1 = 右端）
        /// @param screenY 画面内の縦位置（0 = 上端 / 0.5 = 中央 / 1 = 下端）
        /// @param fov 視野角 [ラジアン]
        /// @param aspectRatio 画面のアスペクト比（0 以下なら 16:9）
        static Vector3 ApplyScreenComposition(const Vector3& rotation,
            float screenX, float screenY, float fov, float aspectRatio);

        /// @brief レール上の位置を求める
        /// @param t 0..1（レール全体を通した位置）
        /// @details 制御点が 1 点ならその点、0 点なら原点を返す。
        static Vector3 EvaluateRail(const std::vector<Vector3>& points, bool loop, float t);

        /// @brief 指定座標に最も近いレール上の位置（0..1）を求める
        /// @details 区間を等間隔に刻んで最も近い所を探す。制御点が 2 点未満なら 0。
        static float ClosestRailParameter(const std::vector<Vector3>& points, bool loop,
            const Vector3& position);

        /// @brief 入力値を 2 点間の視野角へ写す
        /// @param input 距離 [m] または 速さ [m/s]
        /// @return 視野角 [ラジアン]
        static float MapToFov(const CameraRigLens& lens, float input);
    };
}

#pragma once

#include "Camera/CameraStructs.h"

#include <string>
#include <vector>

/// @file
/// @brief カメラリグ（状態駆動のカメラワーク）のデータ型
/// @details シーケンス（CameraSequenceTypes.h）は「時刻」から姿勢を決めるが、こちらは
///          「シーンの状態」から毎フレーム決め直す。TPS の追従や、プレイヤーと敵を
///          まとめて画に収めるカメラは時刻の関数ではないので、キーフレームでは作れない。
///
///          決め方を Body（どこに居るか）/ Aim（どこを向くか）/ Lens（どう写すか）の
///          3 つへ分けてある。互いに独立しているので、組み合わせの数だけ種類が増える
///          のではなく、足した分だけで済む。

namespace CoreEngine
{
    /// @brief リグの既定の置き場所
    namespace CameraRigPaths {
        /// @brief リグ(.json)を探すディレクトリ
        inline constexpr const char* kDirectory = "Application/Assets/Presets/CameraRigs/";
    }

    /// @brief カメラの居場所の決め方
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigBodyMode {
        /// @brief 動かない。指定したワールド座標に据え置く
        Fixed = 0,
        /// @brief 対象からのオフセットに置く（TPS の肩越し・見下ろし）
        FollowTarget = 1,
        /// @brief 対象を中心に、距離・方位角・仰角で回り込む
        OrbitTarget = 2,
        /// @brief 複数の対象をまとめて画に収める位置に置く
        FrameTargets = 3,
        /// @brief 敷いた線の上を滑る（対象に最も近い点、または手動指定）
        Rail = 4
    };

    /// @brief カメラの向きの決め方
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigAimMode {
        /// @brief Body が持つ回転をそのまま使う（向きを固定したいとき）
        FollowBody = 0,
        /// @brief 単一の対象を向く
        LookAtTarget = 1,
        /// @brief 複数の対象の中心を向く
        FrameTargets = 2
    };

    /// @brief 視野角の決め方
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigLensMode {
        /// @brief 固定
        Fixed = 0,
        /// @brief 距離から視野角を出す（離れたら広く、寄ったら狭く）
        DistanceToFov = 1,
        /// @brief 速さから視野角を出す（走ると広がってスピード感が出る）
        SpeedToFov = 2
    };

    /// @brief オフセットをどの軸で解釈するか
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigOffsetSpace {
        /// @brief ワールド軸。対象が振り向いてもカメラの向きは変わらない（見下ろし固定）
        World = 0,
        /// @brief 対象のヨーに追従。対象の背後に付き続ける（TPS）
        Target = 1
    };

    /// @brief DistanceToFov が測る距離の種類
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigDistanceSource {
        /// @brief 注視対象どうしの広がり（2 人の間合い）
        TargetSpread = 0,
        /// @brief カメラから注視先まで
        CameraToAim = 1
    };

    /// @brief 目標への寄せ方
    /// @note 値は JSON にそのまま入る。番号を変えると既存アセットが壊れる。
    enum class CameraRigDampingMode {
        /// @brief 指数減衰。毎フレーム、残りの差の一定割合を詰める
        /// @details 目標が飛んだ瞬間に速度が最大になる。滞りなく追うが、
        ///          グリッド移動のような飛び飛びの目標だと毎回ここでカクつく。
        Exponential = 0,
        /// @brief 臨界減衰のバネ。速度を持ち、それを連続に変えて寄る
        /// @details 目標が飛んでも速度が 0 から立ち上がるので、カクつきが出ない。
        ///          行き過ぎはしない（臨界減衰）。寄る速さの意味は指数減衰と揃えてある。
        Spring = 1
    };

    /// @brief リグが参照する対象 1 件
    struct CameraRigTargetRef {
        /// @brief シーン内のオブジェクト名（空なら無効）
        std::string objectName;

        /// @brief 対象位置に足すオフセット
        /// @details 足元ではなく胸を見る、といった調整に使う。
        Vector3 offset = { 0.0f, 0.0f, 0.0f };

        /// @brief FrameTargets で中心を出すときの重み
        /// @details プレイヤーを重くすると、敵が増えても画がプレイヤー寄りに保たれる。
        float weight = 1.0f;
    };

    /// @brief カメラの居場所の設定
    struct CameraRigBody {
        CameraRigBodyMode mode = CameraRigBodyMode::FollowTarget;

        // ===== Fixed =====

        /// @brief Fixed の据え置き座標
        Vector3 position = { 0.0f, 5.0f, -10.0f };

        /// @brief Aim が FollowBody のときに使う回転 [ラジアン]
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };

        // ===== FollowTarget / OrbitTarget / Rail =====

        /// @brief 主対象（この 3 モードが見る相手）
        CameraRigTargetRef target;

        /// @brief FollowTarget のオフセット
        Vector3 offset = { 0.0f, 3.0f, -6.0f };

        /// @brief オフセットをどの軸で解釈するか
        CameraRigOffsetSpace offsetSpace = CameraRigOffsetSpace::Target;

        /// @brief OrbitTarget の距離 [m]
        float orbitDistance = 8.0f;

        /// @brief OrbitTarget の方位角 [ラジアン]（0 = 対象の背後）
        float orbitYaw = 0.0f;

        /// @brief OrbitTarget の仰角 [ラジアン]（正で上から見下ろす）
        float orbitPitch = 0.2f;

        // ===== FrameTargets =====

        /// @brief まとめて収める対象
        std::vector<CameraRigTargetRef> targets;

        /// @brief どちらの対象へ寄せるか（0 = 先頭 / 0.5 = 中間 / 1 = 末尾）
        /// @details 対象が 2 つ以上あるとき、先頭と末尾を結んだ線上のこの位置を基準にする。
        ///          既定の 0.5 は重み付き中心と一致するので「まん中」になる。
        ///
        ///          軸ごとに指定できるようにしてある。「見るのは 2 人のまん中だが、
        ///          立ち位置は横方向だけ片方へ寄せる」という構図が実際に使われるため。
        ///          全軸を同じ値にすれば、線分上を素直に動く。
        Vector3 frameBias = { 0.5f, 0.5f, 0.5f };

        /// @brief 対象の広がり 1m あたり後ろへ下がる量 [m]
        /// @details 離れたら引いて両方を収める。0 なら距離によらず同じ位置。
        ///          視野角を広げて対応するなら Lens 側の DistanceToFov を使う。
        float framePullBackPerMeter = 0.0f;

        /// @brief 引く量の上限 [m]（0 で無制限）
        /// @details 広がりに比例して下がり続けると、対象が離れただけで画がどこまでも
        ///          引いてしまう。見せたくない外側（未生成の地形など）が画に入るのを
        ///          防ぐなら、ここで引きの範囲を止める。
        float framePullBackMax = 0.0f;

        // ===== Rail =====

        /// @brief レールの制御点（2 点以上で有効）
        std::vector<Vector3> railPoints;

        /// @brief レールを環状に閉じるか
        bool railLoop = false;

        /// @brief レール上の位置を対象から決めるか
        /// @details true なら主対象に最も近い点へ滑る。false なら railPosition を使う。
        bool railFollowTarget = true;

        /// @brief 手動指定のレール位置（0..1、railFollowTarget が false のとき）
        float railPosition = 0.0f;

        /// @brief レール上の位置に足すオフセット
        Vector3 railOffset = { 0.0f, 0.0f, 0.0f };
    };

    /// @brief カメラの向きの設定
    struct CameraRigAim {
        CameraRigAimMode mode = CameraRigAimMode::LookAtTarget;

        /// @brief LookAtTarget で向く相手
        CameraRigTargetRef target;

        /// @brief FrameTargets でまとめて向く相手
        std::vector<CameraRigTargetRef> targets;

        /// @brief 注視先を画面のどこに置くか（0.5, 0.5 = 中央）
        /// @details X は 0 が左端 / 1 が右端、Y は 0 が上端 / 1 が下端。プレイヤーを
        ///          少し下・少し左に置いて前方を広く見せる、といった構図が数値で作れる。
        float screenX = 0.5f;
        float screenY = 0.5f;

        /// @brief 画面の傾き [ラジアン]
        float roll = 0.0f;
    };

    /// @brief 視野角の設定
    struct CameraRigLens {
        CameraRigLensMode mode = CameraRigLensMode::Fixed;

        /// @brief Fixed の視野角 [度]
        float fovDegrees = 45.0f;

        /// @brief 写す入力の下限（距離 [m] または速さ [m/s]）
        float inputMin = 5.0f;

        /// @brief 写す入力の上限
        float inputMax = 30.0f;

        /// @brief inputMin のときの視野角 [度]
        float fovMinDegrees = 35.0f;

        /// @brief inputMax のときの視野角 [度]
        float fovMaxDegrees = 70.0f;

        /// @brief DistanceToFov が測る距離の種類
        CameraRigDistanceSource distanceSource = CameraRigDistanceSource::TargetSpread;
    };

    /// @brief 減衰（急に動かないようにする）の設定
    /// @details 値は「1 秒あたりどれだけ目標へ近づくか」の速さ。0 で減衰なし（即座に一致）。
    ///          どちらの寄せ方でも、フレームレートが変わっても見た目の追従速度は変わらない。
    struct CameraRigDamping {
        /// @brief 位置の追従の速さ [1/秒]
        float position = 5.0f;

        /// @brief 回転の追従の速さ [1/秒]
        float rotation = 5.0f;

        /// @brief 視野角の追従の速さ [1/秒]
        float fov = 5.0f;

        /// @brief 注視先そのものの追従の速さ [1/秒]
        /// @details 対象が跳ねても画がぶれないようにする。回転の減衰とは別に効く。
        float aim = 5.0f;

        /// @brief 寄せ方（位置・注視先・視野角に効く）
        /// @details 向きは位置と注視先から引き直すので、この指定の影響を自然に受ける。
        ///          対象がグリッド単位で飛ぶ（カーソルなど）なら Spring にする。
        CameraRigDampingMode mode = CameraRigDampingMode::Exponential;
    };

    /// @brief 追従を軸ごとに止める設定
    /// @details 対象の座標をここで丸めてから Body も Aim も読む。見下ろしの構図で
    ///          Y と Z を 0 にすると、対象が高い所や奥の列へ行っても画が縦に動かない。
    ///          カメラと注視先が同じ面に残るので、位置だけでなく向きまで据わるのが要点。
    struct CameraRigFollowAxes {
        /// @brief 軸ごとの追従の強さ（1 = そのまま追う / 0 = 動かさない）
        /// @details 間の値は「対象の動きをその割合だけ写す」。0.3 なら 3 割だけ動く。
        Vector3 weight = { 1.0f, 1.0f, 1.0f };

        /// @brief 追従を弱めた軸で据え置くワールド座標
        /// @details 強さ 1 の軸では使わない。0 なら対象の座標をこの値へ置き換える。
        Vector3 anchor = { 0.0f, 0.0f, 0.0f };
    };

    /// @brief カメラリグ 1 本分のデータ
    struct CameraRigAsset {
        /// @brief 保存時に書き込むフォーマットバージョン
        static constexpr const char* kCurrentVersion = "1.0";

        /// @brief 視野角の下限 [度]（0 だと投影行列が壊れる）
        static constexpr float kMinFovDegrees = 1.0f;

        /// @brief 視野角の上限 [度]（180 以上は投影できない）
        static constexpr float kMaxFovDegrees = 179.0f;

        std::string version = kCurrentVersion;

        /// @brief リグ名（一覧や CameraRig::Activate の引数になる）
        std::string name;

        CameraRigBody body;
        CameraRigAim aim;
        CameraRigLens lens;
        CameraRigDamping damping;

        /// @brief 対象の座標を軸ごとに据え置く（構図の面を固定する）
        CameraRigFollowAxes follow;

        /// @brief 各値を扱える範囲へ収める
        /// @details 視野角の上下限、寄り・レール位置の 0..1 クランプ、減衰の非負化、
        ///          入力範囲の下限 < 上限、重み・追従の強さの丸めをまとめて行う。
        ///          読み込んだ後・エディタで触った後は必ずこれを通すこと。
        void Sanitize();
    };
}

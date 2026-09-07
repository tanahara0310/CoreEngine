#include "pch.h"
#include "CameraSequenceEvaluator.h"

#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        struct EasingOption {
            const char* label;
            EasingUtil::Type type;
        };

        constexpr EasingOption kEasingOptions[] = {
            { "線形", EasingUtil::Type::Linear },
            { "イーズイン (Quad)", EasingUtil::Type::EaseInQuad },
            { "イーズアウト (Quad)", EasingUtil::Type::EaseOutQuad },
            { "イーズインアウト (Quad)", EasingUtil::Type::EaseInOutQuad },
            { "イーズイン (Cubic)", EasingUtil::Type::EaseInCubic },
            { "イーズアウト (Cubic)", EasingUtil::Type::EaseOutCubic },
            { "イーズインアウト (Cubic)", EasingUtil::Type::EaseInOutCubic },
            { "イーズイン (Quart)", EasingUtil::Type::EaseInQuart },
            { "イーズアウト (Quart)", EasingUtil::Type::EaseOutQuart },
            { "イーズインアウト (Quart)", EasingUtil::Type::EaseInOutQuart },
            { "イーズイン (Sine)", EasingUtil::Type::EaseInSine },
            { "イーズアウト (Sine)", EasingUtil::Type::EaseOutSine },
            { "イーズインアウト (Sine)", EasingUtil::Type::EaseInOutSine },
            { "イーズイン (Expo)", EasingUtil::Type::EaseInExpo },
            { "イーズアウト (Expo)", EasingUtil::Type::EaseOutExpo },
            { "イーズインアウト (Expo)", EasingUtil::Type::EaseInOutExpo }
        };

        constexpr int kEasingOptionCount = static_cast<int>(sizeof(kEasingOptions) / sizeof(kEasingOptions[0]));

        /// @brief 添字が表の範囲内か
        bool IsValidEasingIndex(int index)
        {
            return index >= 0 && index < kEasingOptionCount;
        }

        /// @brief 一様 Catmull-Rom の基底（Math/Spline/Spline.cpp と同じ式）
        float CatmullRom(float p0, float p1, float p2, float p3, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return p0 * (-0.5f * t3 + t2 - 0.5f * t)
                 + p1 * (1.5f * t3 - 2.5f * t2 + 1.0f)
                 + p2 * (-1.5f * t3 + 2.0f * t2 + 0.5f * t)
                 + p3 * (0.5f * t3 - 0.5f * t2);
        }

        Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
        {
            return {
                CatmullRom(p0.x, p1.x, p2.x, p3.x, t),
                CatmullRom(p0.y, p1.y, p2.y, p3.y, t),
                CatmullRom(p0.z, p1.z, p2.z, p3.z, t)
            };
        }

        /// @brief 基準角から ±π 以内へ寄せた等価な角度を返す
        /// @details 角度を素のままスプラインへ入れると、359 度 → 1 度 の境目で値が
        ///          1 周ぶん飛び、カメラが高速に回ってしまう。
        float Unwrap(float reference, float angle)
        {
            return reference + MathCore::NormalizeAngle(angle - reference);
        }

        /// @brief 4 点のオイラー角を巻き戻してから Catmull-Rom で繋ぐ
        float CatmullRomAngle(float a0, float a1, float a2, float a3, float t)
        {
            // a1 を基準に鎖状へ展開する（隣り合う点どうしが必ず ±π 以内になる）
            const float u1 = a1;
            const float u0 = Unwrap(u1, a0);
            const float u2 = Unwrap(u1, a2);
            const float u3 = Unwrap(u2, a3);
            return MathCore::NormalizeAngle(CatmullRom(u0, u1, u2, u3, t));
        }

        /// @brief 範囲外の添字を端へ丸めてキーを取り出す
        /// @details 端の区間では前後の制御点が無い。端のキーを二重に使うと、
        ///          そこだけ接線がゼロに近づいて自然に止まる。
        const CameraSequenceKeyframe& KeyAt(const std::vector<CameraSequenceKeyframe>& keys, int index)
        {
            const int last = static_cast<int>(keys.size()) - 1;
            return keys[static_cast<size_t>(std::clamp(index, 0, last))];
        }

        /// @brief 投影パラメータのうち、補間で壊れると困る値を均す
        /// @details Catmull-Rom は区間の外へ振れることがある。ニアクリップが 0 以下へ
        ///          振れると射影行列が破綻するので、評価の出口で必ず通す。
        void SanitizeParameters(CameraParameters& parameters)
        {
            if (parameters.nearClip < 0.001f) {
                parameters.nearClip = 0.001f;
            }
            if (parameters.farClip <= parameters.nearClip) {
                parameters.farClip = parameters.nearClip + 0.001f;
            }
            if (parameters.fov <= 0.0f) {
                parameters.fov = 0.0001f;
            }
            if (parameters.aspectRatio < 0.0f) {
                parameters.aspectRatio = 0.0f;
            }
        }

        /// @brief 単一キーの注視を解決し、向きだけ差し替える
        void ApplyKeyAim(const CameraSequenceKeyframe& key, const CameraSequenceAimContext* aim,
            CameraSnapshot& snapshot)
        {
            Vector3 target{};
            if (CameraSequenceEvaluator::ResolveAimTarget(key, aim, target)) {
                snapshot.rotation = CameraSequenceEvaluator::LookRotation(snapshot.position, target, key.aimRoll);
            }
        }

        /// @brief 区間の向きを決める
        ///
        /// @details
        /// 両端のキーそれぞれについて「この視点から見たときの向き」を求め、その 2 つを補間する。
        /// 注視キーなら注視先を向く角度、Euler キーなら保存された角度。
        ///
        /// 区間の向きを始点キーだけで決めると、次のキーが Euler のときに境界で向きが飛ぶ。
        /// 終端では次の区間が始点キー（= こちらの終点キー）の指定で向きを決めるためで、
        /// 両端から求めて混ぜるとその食い違いが原理的に起きない。
        ///
        /// @param position 補間後の視点ワールド座標（向きはここから測る）
        /// @param t 緩急を適用済みの 0..1
        /// @return 両端とも Euler なら false（補間済みの回転をそのまま使うべき）
        bool ResolveSegmentRotation(const CameraSequenceKeyframe& from, const CameraSequenceKeyframe& to,
            const CameraSequenceAimContext* aim, const Vector3& position, float t, Vector3& outRotation)
        {
            Vector3 targetFrom{};
            Vector3 targetTo{};
            const bool fromAims = CameraSequenceEvaluator::ResolveAimTarget(from, aim, targetFrom);
            const bool toAims = CameraSequenceEvaluator::ResolveAimTarget(to, aim, targetTo);

            if (!fromAims && !toAims) {
                return false;
            }

            const Vector3 rotationFrom = fromAims
                ? CameraSequenceEvaluator::LookRotation(position, targetFrom, from.aimRoll)
                : from.snapshot.rotation;
            const Vector3 rotationTo = toAims
                ? CameraSequenceEvaluator::LookRotation(position, targetTo, to.aimRoll)
                : to.snapshot.rotation;

            // t は緩急を適用済みなので、ここでは素直に線形で混ぜる。
            // 角度は LerpAngle で最短経路を通す。
            outRotation = {
                EasingUtil::LerpAngle(rotationFrom.x, rotationTo.x, t, EasingUtil::Type::Linear),
                EasingUtil::LerpAngle(rotationFrom.y, rotationTo.y, t, EasingUtil::Type::Linear),
                EasingUtil::LerpAngle(rotationFrom.z, rotationTo.z, t, EasingUtil::Type::Linear)
            };
            return true;
        }

        /// @brief 区間 [index, index+1] を Catmull-Rom で評価する
        CameraSnapshot InterpolateSmooth(const std::vector<CameraSequenceKeyframe>& keys,
            int index, float t, EasingUtil::Type easing)
        {
            const CameraSnapshot& s0 = KeyAt(keys, index - 1).snapshot;
            const CameraSnapshot& s1 = KeyAt(keys, index).snapshot;
            const CameraSnapshot& s2 = KeyAt(keys, index + 1).snapshot;
            const CameraSnapshot& s3 = KeyAt(keys, index + 2).snapshot;

            // 緩急は「区間内の進み方」、スプラインは「通る道」。先に t を歪めてから道へ乗せる。
            const float easedT = EasingUtil::Apply(t, easing);

            CameraSnapshot result{};
            result.position = CatmullRom(s0.position, s1.position, s2.position, s3.position, easedT);
            result.scale = CatmullRom(s0.scale, s1.scale, s2.scale, s3.scale, easedT);
            result.rotation = {
                CatmullRomAngle(s0.rotation.x, s1.rotation.x, s2.rotation.x, s3.rotation.x, easedT),
                CatmullRomAngle(s0.rotation.y, s1.rotation.y, s2.rotation.y, s3.rotation.y, easedT),
                CatmullRomAngle(s0.rotation.z, s1.rotation.z, s2.rotation.z, s3.rotation.z, easedT)
            };

            result.parameters = s1.parameters;
            result.parameters.fov = CatmullRom(s0.parameters.fov, s1.parameters.fov,
                s2.parameters.fov, s3.parameters.fov, easedT);

            // クリップ距離とアスペクトは行き過ぎると即破綻するので、曲線に乗せず直線で繋ぐ。
            result.parameters.nearClip = EasingUtil::Lerp(
                s1.parameters.nearClip, s2.parameters.nearClip, easedT, EasingUtil::Type::Linear);
            result.parameters.farClip = EasingUtil::Lerp(
                s1.parameters.farClip, s2.parameters.farClip, easedT, EasingUtil::Type::Linear);
            result.parameters.aspectRatio = EasingUtil::Lerp(
                s1.parameters.aspectRatio, s2.parameters.aspectRatio, easedT, EasingUtil::Type::Linear);

            SanitizeParameters(result.parameters);
            return result;
        }
    }

    int CameraSequenceEasing::Count()
    {
        return kEasingOptionCount;
    }

    EasingUtil::Type CameraSequenceEasing::TypeAt(int index)
    {
        return IsValidEasingIndex(index) ? kEasingOptions[index].type : EasingUtil::Type::Linear;
    }

    const char* CameraSequenceEasing::LabelAt(int index)
    {
        return IsValidEasingIndex(index) ? kEasingOptions[index].label : kEasingOptions[0].label;
    }

    bool CameraSequenceEvaluator::Evaluate(const CameraSequenceAsset& asset, float time,
        CameraSnapshot& outSnapshot, const CameraSequenceAimContext* aim)
    {
        if (!EvaluateRaw(asset, time, outSnapshot, aim)) {
            return false;
        }

        // ショット管理有効時は、ショット境界での遷移方式（カット/ブレンド）を適用する。
        if (!asset.shotsEnabled || asset.shots.empty()) {
            return true;
        }

        const float clampedTime = std::clamp(time, 0.0f, asset.timelineLength);
        const int shotIndex = FindShotIndexAt(asset, clampedTime);
        if (shotIndex < 0) {
            return true;
        }

        const CameraSequenceShot& currentShot = asset.shots[shotIndex];
        if (!currentShot.enabled || currentShot.transitionType != CameraSequenceTransitionType::Blend) {
            return true;
        }

        // ブレンド元は「1 つ前の有効なショットの終わりの姿勢」。無ければ繋ぐ相手がいない。
        int previousShotIndex = -1;
        for (int i = shotIndex - 1; i >= 0; --i) {
            if (asset.shots[i].enabled) {
                previousShotIndex = i;
                break;
            }
        }

        if (previousShotIndex < 0) {
            return true;
        }

        const CameraSequenceShot& previousShot = asset.shots[previousShotIndex];
        const float currentShotDuration = (std::max)(currentShot.endTime - currentShot.startTime, 0.0f);
        const float blendDuration = std::clamp(currentShot.blendDuration, 0.0f, currentShotDuration);
        if (blendDuration <= 0.0001f) {
            return true;
        }

        // ブレンドはショット先頭からの一定時間だけ。それ以外は素の評価結果をそのまま使う。
        const float blendStart = currentShot.startTime;
        const float blendEnd = blendStart + blendDuration;
        if (clampedTime < blendStart || clampedTime > blendEnd) {
            return true;
        }

        CameraSnapshot fromSnapshot{};
        if (!EvaluateRaw(asset, previousShot.endTime, fromSnapshot, aim)) {
            return true;
        }

        CameraSnapshot toSnapshot{};
        if (!EvaluateRaw(asset, clampedTime, toSnapshot, aim)) {
            return true;
        }

        // ショット間の繋ぎはシーケンス全体の緩急に従わせる（キーごとの指定は区間内の話）。
        const float blendT = std::clamp((clampedTime - blendStart) / blendDuration, 0.0f, 1.0f);
        outSnapshot = Interpolate(fromSnapshot, toSnapshot, blendT,
            CameraSequenceEasing::TypeAt(asset.easingTypeIndex));
        return true;
    }

    bool CameraSequenceEvaluator::EvaluateRaw(const CameraSequenceAsset& asset, float time,
        CameraSnapshot& outSnapshot, const CameraSequenceAimContext* aim)
    {
        const auto& keyframes = asset.keyframes;

        if (keyframes.empty()) {
            return false;
        }

        if (keyframes.size() == 1) {
            outSnapshot = keyframes.front().snapshot;
            ApplyKeyAim(keyframes.front(), aim, outSnapshot);
            return true;
        }

        const float clampedTime = std::clamp(time, 0.0f, asset.timelineLength);

        // 範囲外は端のキーフレームを返す。注視指定があれば向きだけ解決する
        // （対象が動いていれば、端で止まっていても目は追い続ける）。
        if (clampedTime <= keyframes.front().time) {
            outSnapshot = keyframes.front().snapshot;
            ApplyKeyAim(keyframes.front(), aim, outSnapshot);
            return true;
        }
        if (clampedTime >= keyframes.back().time) {
            outSnapshot = keyframes.back().snapshot;
            ApplyKeyAim(keyframes.back(), aim, outSnapshot);
            return true;
        }

        // 指定時刻が含まれる区間を見つけ、その区間の指定で補間する。
        // 緩急も補間方式も「区間の始点キー」が持つ。
        for (size_t i = 0; i + 1 < keyframes.size(); ++i) {
            const CameraSequenceKeyframe& from = keyframes[i];
            const CameraSequenceKeyframe& to = keyframes[i + 1];

            if (clampedTime < from.time || clampedTime > to.time) {
                continue;
            }

            if (from.interpolation == CameraSequenceInterpolation::Step) {
                outSnapshot = from.snapshot;
                return true;
            }

            const float span = to.time - from.time;
            if (span <= 0.0001f) {
                outSnapshot = from.snapshot;
                return true;
            }

            const float t = (clampedTime - from.time) / span;
            const EasingUtil::Type easing = ResolveEasing(asset, from);

            if (from.interpolation == CameraSequenceInterpolation::Smooth) {
                outSnapshot = InterpolateSmooth(keyframes, static_cast<int>(i), t, easing);
            } else {
                outSnapshot = Interpolate(from.snapshot, to.snapshot, t, easing);
            }

            // 向きは位置が決まった「後」に解決する。視線は補間後の視点から測るため、
            // ここで上書きしないと、回り込みの途中で対象から目が離れる。
            Vector3 aimedRotation{};
            if (ResolveSegmentRotation(from, to, aim, outSnapshot.position,
                    EasingUtil::Apply(t, easing), aimedRotation)) {
                outSnapshot.rotation = aimedRotation;
            }
            return true;
        }

        outSnapshot = keyframes.back().snapshot;
        ApplyKeyAim(keyframes.back(), aim, outSnapshot);
        return true;
    }

    // 2 つのスナップショットを補間する。回転だけは LerpAngle で最短経路を通す
    // （素直に線形補間すると 359°→1° で 1 周してしまう）
    CameraSnapshot CameraSequenceEvaluator::Interpolate(const CameraSnapshot& from, const CameraSnapshot& to,
        float t, EasingUtil::Type easing)
    {
        CameraSnapshot result{};

        result.position = EasingUtil::LerpVector3(from.position, to.position, t, easing);
        result.rotation = Vector3(
            EasingUtil::LerpAngle(from.rotation.x, to.rotation.x, t, easing),
            EasingUtil::LerpAngle(from.rotation.y, to.rotation.y, t, easing),
            EasingUtil::LerpAngle(from.rotation.z, to.rotation.z, t, easing));
        result.scale = EasingUtil::LerpVector3(from.scale, to.scale, t, easing);

        // 投影方式は補間できないので、始点のものを引き継ぐ。
        result.parameters.projectionType = from.parameters.projectionType;
        result.parameters.fov = EasingUtil::Lerp(from.parameters.fov, to.parameters.fov, t, easing);
        result.parameters.nearClip = EasingUtil::Lerp(from.parameters.nearClip, to.parameters.nearClip, t, easing);
        result.parameters.farClip = EasingUtil::Lerp(from.parameters.farClip, to.parameters.farClip, t, easing);
        result.parameters.aspectRatio = EasingUtil::Lerp(from.parameters.aspectRatio, to.parameters.aspectRatio, t, easing);

        SanitizeParameters(result.parameters);
        return result;
    }

    EasingUtil::Type CameraSequenceEvaluator::ResolveEasing(const CameraSequenceAsset& asset,
        const CameraSequenceKeyframe& key)
    {
        // キーが既定指定（kUseSequenceEasing）ならシーケンス側の値を使う。
        // 旧フォーマットのアセットはキー側を持たないので、従来どおりの見た目になる。
        const int index = (key.easingTypeIndex == kUseSequenceEasing)
            ? asset.easingTypeIndex
            : key.easingTypeIndex;
        return CameraSequenceEasing::TypeAt(index);
    }

    bool CameraSequenceEvaluator::ResolveAimTarget(const CameraSequenceKeyframe& key,
        const CameraSequenceAimContext* aim, Vector3& outTarget)
    {
        switch (key.aimMode) {
        case CameraSequenceAimMode::LookAtPoint:
            outTarget = key.aimPoint + key.aimOffset;
            return true;

        case CameraSequenceAimMode::LookAtObject: {
            // 解決できないときは false。呼び出し側は保存された回転へ落ちるので、
            // 対象が消えてもシーケンスは authored の向きで再生され続ける。
            if (!aim || !aim->resolveObject || key.aimObjectName.empty()) {
                return false;
            }
            Vector3 objectPosition{};
            if (!aim->resolveObject(key.aimObjectName, objectPosition)) {
                return false;
            }
            outTarget = objectPosition + key.aimOffset;
            return true;
        }

        case CameraSequenceAimMode::Euler:
        default:
            return false;
        }
    }

    Vector3 CameraSequenceEvaluator::LookRotation(const Vector3& eye, const Vector3& target, float roll)
    {
        const Vector3 delta = target - eye;
        if (LengthSquared(delta) <= 1.0e-12f) {
            // 視点と注視先が重なると向きが決まらない。真正面を向かせて発散を防ぐ。
            return { 0.0f, 0.0f, roll };
        }

        const Vector3 forward = Normalize(delta);

        // MakeAffine の回転は Rx * Ry * Rz（行ベクトル規約）。roll = 0 のとき
        // 第 3 行（前方軸）は (cosX sinY, -sinX, cosX cosY) になるので、
        // yaw = atan2(f.x, f.z) / pitch = asin(-f.y) が Camera::LookAt と厳密に一致する。
        return {
            std::asin(std::clamp(-forward.y, -1.0f, 1.0f)),
            std::atan2(forward.x, forward.z),
            roll
        };
    }

    int CameraSequenceEvaluator::FindShotIndexAt(const CameraSequenceAsset& asset, float time)
    {
        for (int i = 0; i < static_cast<int>(asset.shots.size()); ++i) {
            const CameraSequenceShot& shot = asset.shots[i];
            if (!shot.enabled) {
                continue;
            }

            if (time >= shot.startTime && time <= shot.endTime) {
                return i;
            }
        }
        return -1;
    }
}

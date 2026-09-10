#include "pch.h"
#include "CameraRigEvaluator.h"

#include "Math/Easing/EasingUtil.h"
#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CoreEngine
{
    namespace
    {
        /// @brief アスペクト比が未指定（0 以下）のときに使う既定値
        /// @details 実行時は実画面から入るが、エディタのプレビューや単体試験では
        ///          埋まっていないことがある。読めれば十分な 16:9 にしておく。
        constexpr float kFallbackAspectRatio = 16.0f / 9.0f;

        /// @brief レールの最近点を探すときの 1 区間あたりの刻み数
        /// @details 細かくすれば近づくが毎フレーム回る。カメラの位置決めとしては
        ///          これで十分で、足りない分は減衰が均す。
        constexpr int kRailSamplesPerSegment = 16;

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

        Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2,
            const Vector3& p3, float t)
        {
            return {
                CatmullRom(p0.x, p1.x, p2.x, p3.x, t),
                CatmullRom(p0.y, p1.y, p2.y, p3.y, t),
                CatmullRom(p0.z, p1.z, p2.z, p3.z, t)
            };
        }

        /// @brief 範囲外の添字を端へ丸める／環状なら巻き戻して制御点を取り出す
        const Vector3& RailPointAt(const std::vector<Vector3>& points, bool loop, int index)
        {
            const int count = static_cast<int>(points.size());
            if (loop) {
                // C++ の % は負で負を返す。足してから取り直す。
                return points[static_cast<size_t>(((index % count) + count) % count)];
            }
            return points[static_cast<size_t>(std::clamp(index, 0, count - 1))];
        }

        /// @brief 追従を弱めた軸を基準座標へ引き戻す
        float FollowAxis(float value, float anchor, float weight)
        {
            // 1 は「そのまま追う」。掛けて足すと丸めで最下位桁がずれるので素通しにする。
            return (weight >= 1.0f) ? value : anchor + (value - anchor) * weight;
        }

        /// @brief 対象の座標を軸ごとに据え置く
        Vector3 ApplyFollowAxes(const CameraRigFollowAxes& follow, const Vector3& position)
        {
            return {
                FollowAxis(position.x, follow.anchor.x, follow.weight.x),
                FollowAxis(position.y, follow.anchor.y, follow.weight.y),
                FollowAxis(position.z, follow.anchor.z, follow.weight.z)
            };
        }

        /// @brief 対象を解決してオフセットを足した座標を得る
        /// @note 返す座標には追従の据え置きが掛かる。outState は素の値のままで、
        ///       速度（SpeedToFov）と向き（オフセットの回転）はそちらを読む。
        bool ResolveTargetPosition(const CameraRigTargetRef& ref, const CameraRigContext* context,
            const CameraRigFollowAxes& follow,
            Vector3& outPosition, CameraRigTargetState* outState = nullptr)
        {
            if (ref.objectName.empty() || context == nullptr || !context->resolveTarget) {
                return false;
            }

            CameraRigTargetState state{};
            if (!context->resolveTarget(ref.objectName, state)) {
                return false;
            }

            outPosition = ApplyFollowAxes(follow, state.position + ref.offset);
            if (outState != nullptr) {
                *outState = state;
            }
            return true;
        }

        /// @brief 対象の重み付き中心と、先頭・末尾の座標を求める
        /// @return 1 件も解決できなければ false
        bool ResolveTargetGroup(const std::vector<CameraRigTargetRef>& refs,
            const CameraRigContext* context, const CameraRigFollowAxes& follow,
            Vector3& outCenter, Vector3& outFirst, Vector3& outLast, float& outSpread)
        {
            Vector3 weightedSum{ 0.0f, 0.0f, 0.0f };
            float totalWeight = 0.0f;
            bool hasAny = false;

            // 解決できたものだけを集める。1 体が死んで消えてもカメラは残りで成立させる。
            std::vector<Vector3> resolved;
            resolved.reserve(refs.size());

            for (const auto& ref : refs) {
                Vector3 position{};
                if (!ResolveTargetPosition(ref, context, follow, position)) {
                    continue;
                }
                resolved.push_back(position);
                weightedSum += position * ref.weight;
                totalWeight += ref.weight;
                hasAny = true;
            }

            if (!hasAny) {
                return false;
            }

            // 重みが全て 0 なら素の平均へ落とす。重み 0 は「数に入れない」意味だが、
            // 全部 0 のときまで評価を失敗させる理由はない。
            if (totalWeight > 1.0e-6f) {
                outCenter = weightedSum * (1.0f / totalWeight);
            } else {
                Vector3 sum{ 0.0f, 0.0f, 0.0f };
                for (const auto& position : resolved) {
                    sum += position;
                }
                outCenter = sum * (1.0f / static_cast<float>(resolved.size()));
            }

            outFirst = resolved.front();
            outLast = resolved.back();

            // 広がりは、集めた点のうち最も離れた 2 点の距離。対象が 3 体以上でも
            // 「全員を収めるのにどれだけ要るか」に対応する。
            outSpread = 0.0f;
            for (size_t i = 0; i < resolved.size(); ++i) {
                for (size_t j = i + 1; j < resolved.size(); ++j) {
                    outSpread = (std::max)(outSpread, Distance(resolved[i], resolved[j]));
                }
            }
            return true;
        }

        /// @brief オイラー角から基底行列を作る
        Matrix4x4 MakeBasis(const Vector3& rotation)
        {
            return MathCore::Matrix::MakeAffine(
                Vector3{ 1.0f, 1.0f, 1.0f }, rotation, Vector3{ 0.0f, 0.0f, 0.0f });
        }

        /// @brief 対象のヨーだけを見てオフセットを回す
        /// @details ピッチまで追うと、対象が坂を向いただけでカメラが空や地面へ潜る。
        Vector3 RotateOffsetByTargetYaw(const Vector3& offset, const Vector3& targetRotation)
        {
            const Matrix4x4 basis = MakeBasis(Vector3{ 0.0f, targetRotation.y, 0.0f });
            return basis.GetAxisX() * offset.x
                 + basis.GetAxisY() * offset.y
                 + basis.GetAxisZ() * offset.z;
        }
    }

    float CameraRigEvaluator::DampingFactor(float speed, float deltaTime)
    {
        if (speed <= 0.0f || deltaTime <= 0.0f) {
            // 減衰なし。目標へそのまま置く。
            return 1.0f;
        }
        return 1.0f - std::exp(-speed * deltaTime);
    }

    void CameraRigEvaluator::SpringDamp(float& current, float& velocity, float target,
        float speed, float deltaTime)
    {
        if (speed <= 0.0f || deltaTime <= 0.0f) {
            // 減衰なし。目標へそのまま置く。
            current = target;
            velocity = 0.0f;
            return;
        }

        // 固有角振動数。speed の意味を指数減衰と揃えるために 2 倍する。
        // こうすると 1/speed 秒で 6 割ほど詰まる点が両方で一致する。
        const float omega = 2.0f * speed;
        const float x = omega * deltaTime;

        // exp(-x) の有理式近似。フレームが長くなっても発散しない形にしてある。
        const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        const float difference = current - target;
        const float temp = (velocity + omega * difference) * deltaTime;
        velocity = (velocity - omega * temp) * decay;
        current = target + (difference + temp) * decay;
    }

    void CameraRigEvaluator::SpringDamp(Vector3& current, Vector3& velocity, const Vector3& target,
        float speed, float deltaTime)
    {
        SpringDamp(current.x, velocity.x, target.x, speed, deltaTime);
        SpringDamp(current.y, velocity.y, target.y, speed, deltaTime);
        SpringDamp(current.z, velocity.z, target.z, speed, deltaTime);
    }

    Vector3 CameraRigEvaluator::LookRotation(const Vector3& eye, const Vector3& target, float roll)
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

    Vector3 CameraRigEvaluator::ApplyScreenComposition(const Vector3& rotation,
        float screenX, float screenY, float fov, float aspectRatio)
    {
        const float aspect = (aspectRatio > 0.0f) ? aspectRatio : kFallbackAspectRatio;
        const float halfHeight = std::tan(fov * 0.5f);
        const float halfWidth = halfHeight * aspect;

        // 注視先を画面中央から (screenX, screenY) へずらすには、カメラを逆向きへ振る。
        // 中央からのずれは距離によらず一定の角度になるので、tan で角度へ直せる。
        // ピッチは正で見下ろすので、対象を画面の上（screenY < 0.5）へ出すには増やす。
        const float pitch = std::atan((0.5f - screenY) * 2.0f * halfHeight);
        const float yaw = std::atan((0.5f - screenX) * 2.0f * halfWidth);

        return {
            MathCore::NormalizeAngle(rotation.x + pitch),
            MathCore::NormalizeAngle(rotation.y + yaw),
            rotation.z
        };
    }

    Vector3 CameraRigEvaluator::EvaluateRail(const std::vector<Vector3>& points, bool loop, float t)
    {
        if (points.empty()) {
            return { 0.0f, 0.0f, 0.0f };
        }
        if (points.size() == 1) {
            return points.front();
        }

        const int count = static_cast<int>(points.size());
        // 閉じたレールは点の数だけ区間がある。開いたレールは 1 つ少ない。
        const int segments = loop ? count : (count - 1);

        const float clamped = std::clamp(t, 0.0f, 1.0f);
        const float scaled = clamped * static_cast<float>(segments);

        int segment = static_cast<int>(scaled);
        // t == 1 のとき添字が範囲を越える。最終区間の終端へ寄せる。
        segment = (std::min)(segment, segments - 1);
        const float local = scaled - static_cast<float>(segment);

        return CatmullRom(
            RailPointAt(points, loop, segment - 1),
            RailPointAt(points, loop, segment),
            RailPointAt(points, loop, segment + 1),
            RailPointAt(points, loop, segment + 2),
            local);
    }

    float CameraRigEvaluator::ClosestRailParameter(const std::vector<Vector3>& points, bool loop,
        const Vector3& position)
    {
        if (points.size() < 2) {
            return 0.0f;
        }

        const int segments = loop
            ? static_cast<int>(points.size())
            : static_cast<int>(points.size()) - 1;
        const int sampleCount = segments * kRailSamplesPerSegment;

        float bestT = 0.0f;
        float bestDistanceSquared = (std::numeric_limits<float>::max)();

        for (int i = 0; i <= sampleCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(sampleCount);
            const float distanceSquared = DistanceSquared(EvaluateRail(points, loop, t), position);
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestT = t;
            }
        }
        return bestT;
    }

    float CameraRigEvaluator::MapToFov(const CameraRigLens& lens, float input)
    {
        const float span = lens.inputMax - lens.inputMin;
        // Sanitize を通っていれば span > 0。通っていないアセットでもゼロ除算しない。
        const float rate = (span > 1.0e-6f)
            ? std::clamp((input - lens.inputMin) / span, 0.0f, 1.0f)
            : 0.0f;
        const float degrees = lens.fovMinDegrees + (lens.fovMaxDegrees - lens.fovMinDegrees) * rate;
        return degrees * MathCore::Constants::kDegToRad;
    }

    bool CameraRigEvaluator::EvaluateDesired(const CameraRigAsset& asset,
        const CameraRigContext* context, CameraRigPose& outPose)
    {
        CameraRigPose pose{};

        // ===== 1. Body: どこに居るか =====
        // 対象の広がりは Body でも Lens でも使うので、ここで拾って持ち回す。
        float bodySpread = 0.0f;

        switch (asset.body.mode) {
        case CameraRigBodyMode::Fixed: {
            pose.position = asset.body.position;
            pose.rotation = asset.body.rotation;
            break;
        }

        case CameraRigBodyMode::FollowTarget: {
            Vector3 targetPosition{};
            CameraRigTargetState targetState{};
            if (!ResolveTargetPosition(asset.body.target, context, asset.follow,
                targetPosition, &targetState)) {
                return false;
            }

            const Vector3 offset = (asset.body.offsetSpace == CameraRigOffsetSpace::Target)
                ? RotateOffsetByTargetYaw(asset.body.offset, targetState.rotation)
                : asset.body.offset;
            pose.position = targetPosition + offset;
            break;
        }

        case CameraRigBodyMode::OrbitTarget: {
            Vector3 targetPosition{};
            CameraRigTargetState targetState{};
            if (!ResolveTargetPosition(asset.body.target, context, asset.follow,
                targetPosition, &targetState)) {
                return false;
            }

            // 対象の背後を 0 とするので、対象のヨーへ足してから方向を作る。
            const float yaw = targetState.rotation.y + asset.body.orbitYaw;
            const float pitch = asset.body.orbitPitch;

            // 対象から見た「カメラのある方向」。仰角が正なら上へ。
            const Vector3 direction{
                -std::sin(yaw) * std::cos(pitch),
                 std::sin(pitch),
                -std::cos(yaw) * std::cos(pitch)
            };
            pose.position = targetPosition + direction * asset.body.orbitDistance;
            break;
        }

        case CameraRigBodyMode::FrameTargets: {
            Vector3 center{};
            Vector3 first{};
            Vector3 last{};
            if (!ResolveTargetGroup(asset.body.targets, context, asset.follow,
                center, first, last, bodySpread)) {
                return false;
            }

            // 寄り 0.5 は先頭と末尾の中点＝重み一定なら中心と一致する。既定で「まん中」。
            // 軸ごとに寄せられるので、「Y と Z は中点のまま X だけ片方へ寄せる」が作れる。
            const Vector3 span = last - first;
            const Vector3 anchor = (first == last)
                ? center
                : Vector3{
                    first.x + span.x * asset.body.frameBias.x,
                    first.y + span.y * asset.body.frameBias.y,
                    first.z + span.z * asset.body.frameBias.z
                  };

            pose.position = anchor + asset.body.offset;

            // 離れたら引いて両方を収める。オフセットの向きへ後退するので画角は保たれる。
            if (asset.body.framePullBackPerMeter > 0.0f
                && LengthSquared(asset.body.offset) > 1.0e-12f) {
                float pullBack = bodySpread * asset.body.framePullBackPerMeter;

                // 上限を入れると、対象がどれだけ離れても画に入る範囲がここで止まる。
                // 見せたくない外側を画へ入れないための歯止め。
                if (asset.body.framePullBackMax > 0.0f) {
                    pullBack = (std::min)(pullBack, asset.body.framePullBackMax);
                }

                const Vector3 back = Normalize(asset.body.offset);
                pose.position += back * pullBack;
            }
            break;
        }

        case CameraRigBodyMode::Rail: {
            if (asset.body.railPoints.size() < 2) {
                return false;
            }

            float t = asset.body.railPosition;
            if (asset.body.railFollowTarget) {
                Vector3 targetPosition{};
                if (!ResolveTargetPosition(asset.body.target, context, asset.follow,
                    targetPosition)) {
                    return false;
                }
                t = ClosestRailParameter(asset.body.railPoints, asset.body.railLoop, targetPosition);
            }

            pose.position = EvaluateRail(asset.body.railPoints, asset.body.railLoop, t)
                + asset.body.railOffset;
            break;
        }
        }

        // ===== 2. Aim: どこを向くか（注視先だけ先に出す） =====
        // 回転は視野角が決まらないと画面内の位置を合わせられないので、ここでは座標まで。
        float aimSpread = 0.0f;

        switch (asset.aim.mode) {
        case CameraRigAimMode::FollowBody:
            // Body の回転をそのまま使う。注視先は無い。
            break;

        case CameraRigAimMode::LookAtTarget: {
            Vector3 aimPosition{};
            if (!ResolveTargetPosition(asset.aim.target, context, asset.follow, aimPosition)) {
                return false;
            }
            pose.aimPoint = aimPosition;
            pose.hasAimPoint = true;
            break;
        }

        case CameraRigAimMode::FrameTargets: {
            Vector3 center{};
            Vector3 first{};
            Vector3 last{};
            if (!ResolveTargetGroup(asset.aim.targets, context, asset.follow,
                center, first, last, aimSpread)) {
                return false;
            }
            pose.aimPoint = center;
            pose.hasAimPoint = true;
            break;
        }
        }

        // ===== 3. Lens: どう写すか =====
        switch (asset.lens.mode) {
        case CameraRigLensMode::Fixed:
            pose.fov = asset.lens.fovDegrees * MathCore::Constants::kDegToRad;
            break;

        case CameraRigLensMode::DistanceToFov: {
            float distance = 0.0f;
            if (asset.lens.distanceSource == CameraRigDistanceSource::CameraToAim) {
                // 注視先が無ければ測れない。固定値へ落として破綻を避ける。
                distance = pose.hasAimPoint ? Distance(pose.position, pose.aimPoint)
                                            : asset.lens.inputMin;
            } else {
                // 対象どうしの広がり。Aim 側を優先し、無ければ Body 側を使う。
                distance = (aimSpread > 0.0f) ? aimSpread : bodySpread;
            }
            pose.fov = MapToFov(asset.lens, distance);
            break;
        }

        case CameraRigLensMode::SpeedToFov: {
            // 速さは注視対象のもの。注視が無ければ Body の主対象を見る。
            const CameraRigTargetRef& ref = (asset.aim.mode == CameraRigAimMode::LookAtTarget)
                ? asset.aim.target
                : asset.body.target;

            Vector3 ignored{};
            CameraRigTargetState state{};
            const bool resolved = ResolveTargetPosition(ref, context, asset.follow,
                ignored, &state);

            // 速度を持たないシーンでは 0 とみなす。fovMin 側に張り付くだけで壊れない。
            const float speed = (resolved && state.hasVelocity) ? Length(state.velocity) : 0.0f;
            pose.fov = MapToFov(asset.lens, speed);
            break;
        }
        }

        // ===== 4. 注視先から回転を作り、画面内の位置を合わせる =====
        if (pose.hasAimPoint) {
            pose.rotation = LookRotation(pose.position, pose.aimPoint, asset.aim.roll);
            pose.rotation = ApplyScreenComposition(pose.rotation,
                asset.aim.screenX, asset.aim.screenY, pose.fov,
                (context != nullptr) ? context->aspectRatio : 0.0f);
        }

        outPose = pose;
        return true;
    }

    void CameraRigEvaluator::ApplyDamping(const CameraRigAsset& asset, const CameraRigPose& desired,
        float deltaTime, CameraRigState& state)
    {
        if (!state.initialized) {
            // 初回に減衰を掛けると、原点から目標へ滑り込む見苦しい動きになる。
            state.position = desired.position;
            state.rotation = desired.rotation;
            state.fov = desired.fov;
            state.aimPoint = desired.aimPoint;
            state.positionVelocity = { 0.0f, 0.0f, 0.0f };
            state.aimVelocity = { 0.0f, 0.0f, 0.0f };
            state.fovVelocity = 0.0f;
            state.initialized = true;
            return;
        }

        const bool useSpring = (asset.damping.mode == CameraRigDampingMode::Spring);

        if (useSpring) {
            SpringDamp(state.position, state.positionVelocity, desired.position,
                asset.damping.position, deltaTime);
            SpringDamp(state.fov, state.fovVelocity, desired.fov,
                asset.damping.fov, deltaTime);
        } else {
            state.position += (desired.position - state.position)
                * DampingFactor(asset.damping.position, deltaTime);
            state.fov += (desired.fov - state.fov)
                * DampingFactor(asset.damping.fov, deltaTime);
        }

        const float rotationRate = DampingFactor(asset.damping.rotation, deltaTime);

        Vector3 targetRotation = desired.rotation;
        if (desired.hasAimPoint) {
            // 注視先そのものを鈍らせてから向きを引き直す。対象が跳ねても画がぶれない。
            // 向きは位置と注視先から引き直すので、両方をバネで寄せれば向きも連続になる。
            if (useSpring) {
                SpringDamp(state.aimPoint, state.aimVelocity, desired.aimPoint,
                    asset.damping.aim, deltaTime);
            } else {
                state.aimPoint += (desired.aimPoint - state.aimPoint)
                    * DampingFactor(asset.damping.aim, deltaTime);
            }
            targetRotation = LookRotation(state.position, state.aimPoint, asset.aim.roll);
            targetRotation = ApplyScreenComposition(targetRotation,
                asset.aim.screenX, asset.aim.screenY, state.fov, 0.0f);
        }

        // 角度は素の差で寄せると 359 度 → 1 度 の境目で 1 周ぶん回る。
        state.rotation = {
            EasingUtil::LerpAngle(state.rotation.x, targetRotation.x, rotationRate),
            EasingUtil::LerpAngle(state.rotation.y, targetRotation.y, rotationRate),
            EasingUtil::LerpAngle(state.rotation.z, targetRotation.z, rotationRate)
        };
    }

    bool CameraRigEvaluator::Evaluate(const CameraRigAsset& asset, const CameraRigContext* context,
        float deltaTime, CameraRigState& state, CameraSnapshot& outSnapshot)
    {
        CameraRigPose desired{};
        if (!EvaluateDesired(asset, context, desired)) {
            return false;
        }

        ApplyDamping(asset, desired, deltaTime, state);

        outSnapshot.position = state.position;
        outSnapshot.rotation = state.rotation;
        outSnapshot.parameters.fov = state.fov;
        return true;
    }
}

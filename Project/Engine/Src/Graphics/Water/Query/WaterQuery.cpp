#include "pch.h"
#include "WaterQuery.h"

#include <algorithm>
#include <cmath>

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    uint32_t WaterQuery::AddPoint(const Vector3& worldPosition)
    {
        uint32_t point = kInvalidPoint;
        if (!freePoints_.empty()) {
            point = freePoints_.back();
            freePoints_.pop_back();
        } else if (points_.size() < kMaxPoints) {
            point = static_cast<uint32_t>(points_.size());
            points_.emplace_back();
        } else {
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterQuery: 水面の高さを聞く点が上限 {} 個に達したので、これ以上は足せません", kMaxPoints);
            return kInvalidPoint;
        }

        Point& slot = points_[point];
        slot.active = true;
        ++slot.generation;
        slot.x = worldPosition.x;
        slot.z = worldPosition.z;
        slot.hasResult = false;
        return point;
    }

    void WaterQuery::RemovePoint(uint32_t point)
    {
        if (point >= points_.size() || !points_[point].active) {
            return;
        }
        Point& slot = points_[point];
        slot.active = false;
        slot.hasResult = false;
        freePoints_.push_back(point);
    }

    void WaterQuery::SetPosition(uint32_t point, const Vector3& worldPosition)
    {
        if (point >= points_.size() || !points_[point].active) {
            return;
        }
        points_[point].x = worldPosition.x;
        points_[point].z = worldPosition.z;
    }

    WaterSample WaterQuery::Sample(uint32_t point, const Vector3& worldPosition) const
    {
        WaterSample sample{};
        if (point >= points_.size()) {
            return sample;
        }
        const Point& slot = points_[point];
        if (!slot.active || !slot.hasResult || (slot.result.flags & kGpuResultInWater) == 0) {
            return sample;
        }

        // 評価した位置から今の位置まで、水面の傾きで延ばす（遠すぎる分は延ばさない）
        float deltaX = worldPosition.x - slot.evaluatedAt.x;
        float deltaZ = worldPosition.z - slot.evaluatedAt.z;
        const float distance = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
        if (distance > kMaxExtrapolationMeters) {
            const float scale = kMaxExtrapolationMeters / distance;
            deltaX *= scale;
            deltaZ *= scale;
        }

        sample.inWater = true;
        sample.surfaceHeight = slot.result.height + slot.result.slopeX * deltaX + slot.result.slopeZ * deltaZ;
        sample.normal = Normalize(Vector3{ -slot.result.slopeX, 1.0f, -slot.result.slopeZ });
        return sample;
    }

    void WaterQuery::Snapshot(std::vector<Submitted>& out) const
    {
        out.resize(points_.size());
        for (size_t i = 0; i < points_.size(); ++i) {
            out[i].x = points_[i].x;
            out[i].z = points_[i].z;
            out[i].generation = points_[i].active ? points_[i].generation : 0u;
        }
    }

    void WaterQuery::ApplyResults(std::span<const Submitted> submitted, std::span<const GpuResult> results)
    {
        const size_t count = (std::min)({ submitted.size(), results.size(), points_.size() });
        for (size_t i = 0; i < count; ++i) {
            Point& slot = points_[i];
            if (!slot.active || submitted[i].generation != slot.generation) {
                continue;
            }
            slot.hasResult = true;
            slot.evaluatedAt = submitted[i];
            slot.result = results[i];
        }
    }

    void WaterQuery::ClearResults()
    {
        for (Point& slot : points_) {
            slot.hasResult = false;
        }
    }

    WaterQuery* FindWaterQuery(const GameObject* object)
    {
        EngineSystem* const engine = object ? object->GetEngineSystem() : nullptr;
        RenderDomainContext* const domain = engine ? engine->GetRenderDomainContext() : nullptr;
        return domain ? domain->GetWaterQuery() : nullptr;
    }
}

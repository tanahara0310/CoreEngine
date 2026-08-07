#include "pch.h"
#include "CollisionProbeObject.h"
#include "CollisionTestReport.h"

namespace CollisionTest
{
    namespace {
        /// @brief 相手の表示名（破棄済み・未設定でも落ちないように）
        const char* OtherName(CoreEngine::GameObject* other)
        {
            return other ? other->GetDisplayName() : "(null)";
        }
    }

    namespace ProbeEvents
    {
        void OnEnter(const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other)
        {
            ++stats.enter;
            ++stats.overlapDepth;
            LogEvent(label + " Enter <- " + OtherName(other));
        }

        void OnStay(const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other)
        {
            (void)label;
            (void)other;
            // Stay は毎フレーム呼ばれるためログには出さない（ログが埋まる）
            ++stats.stay;
        }

        void OnExit(const std::string& label, ProbeStats& stats, CoreEngine::GameObject* other)
        {
            ++stats.exit;
            --stats.overlapDepth;
            LogEvent(label + " Exit  <- " + OtherName(other));
        }

        bool IsRemoveColliderInCallbackEnabled()
        {
            return Report::Get().IsRemoveColliderInCallbackEnabled();
        }
    }
}

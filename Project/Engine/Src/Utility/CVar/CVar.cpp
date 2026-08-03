#include "pch.h"
#include "CVar.h"
#include "CVarRegistry.h"

namespace CoreEngine
{
    // ──────────────────────────────────────────────────────────────
    // ICVar
    // ──────────────────────────────────────────────────────────────

    ICVar::ICVar(const char* name, const char* description, CVarType type,
                 void* storage, CVarRange range, CVarFlags flags)
        : name_(name ? name : "")
        , description_(description ? description : "")
        , type_(type)
        , storage_(storage)
        , range_(range)
        , flags_(flags)
    {
    }

    ICVar::~ICVar()
    {
        if (registered_) {
            CVarRegistry::Get().Unregister(this);
        }
    }

    void ICVar::RegisterSelf()
    {
        CVarRegistry::Get().Register(this);
        registered_ = true;
    }

    void ICVar::NotifyChanged()
    {
        ++revision_;
        CVarRegistry::Get().OnCVarChanged(this);
        if (onChanged_) {
            onChanged_();
        }
    }

    // ──────────────────────────────────────────────────────────────
    // 値の等価判定（Vector3 / Vector4 に operator== が無いため個別に用意）
    // ──────────────────────────────────────────────────────────────

    namespace detail
    {
        bool CVarValueEquals(bool a, bool b) noexcept { return a == b; }
        bool CVarValueEquals(int a, int b) noexcept { return a == b; }
        bool CVarValueEquals(float a, float b) noexcept { return a == b; }

        bool CVarValueEquals(const Vector2& a, const Vector2& b) noexcept
        {
            return a.x == b.x && a.y == b.y;
        }

        bool CVarValueEquals(const Vector3& a, const Vector3& b) noexcept
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        bool CVarValueEquals(const Vector4& a, const Vector4& b) noexcept
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }
    }
}

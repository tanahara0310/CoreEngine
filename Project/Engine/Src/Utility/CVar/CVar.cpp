#include "pch.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include <cstdio>

namespace CoreEngine
{
    // ──────────────────────────────────────────────────────────────
    // ICVar
    // ──────────────────────────────────────────────────────────────

    ICVar::ICVar(const char* name, const char* description, CVarType type,
                 void* storage, const void* defaultStorage, CVarRange range, CVarFlags flags)
        : name_(name ? name : "")
        , description_(description ? description : "")
        , type_(type)
        , storage_(storage)
        , defaultStorage_(defaultStorage)
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
    // 値の文字列化（起動時の上書き一覧ログなどに使う）
    // ──────────────────────────────────────────────────────────────

    namespace
    {
        /// @brief float を "1.35" のような最短表現にする（%g。末尾ゼロを引きずらない）
        std::string FormatFloat(float v)
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%g", v);
            return buffer;
        }

        /// @brief 型消去された値を文字列化する
        std::string FormatValue(CVarType type, const void* p)
        {
            if (!p) {
                return "(null)";
            }
            switch (type)
            {
            case CVarType::Bool:
                return *static_cast<const bool*>(p) ? "true" : "false";
            case CVarType::Int:
                return std::to_string(*static_cast<const int*>(p));
            case CVarType::Float:
                return FormatFloat(*static_cast<const float*>(p));
            case CVarType::Vector2: {
                const auto& v = *static_cast<const Vector2*>(p);
                return "[" + FormatFloat(v.x) + ", " + FormatFloat(v.y) + "]";
            }
            case CVarType::Vector3: {
                const auto& v = *static_cast<const Vector3*>(p);
                return "[" + FormatFloat(v.x) + ", " + FormatFloat(v.y) + ", " + FormatFloat(v.z) + "]";
            }
            case CVarType::Color: {
                const auto& v = *static_cast<const Vector4*>(p);
                return "[" + FormatFloat(v.x) + ", " + FormatFloat(v.y) + ", "
                     + FormatFloat(v.z) + ", " + FormatFloat(v.w) + "]";
            }
            }
            return "(unknown)";
        }
    }

    std::string ICVar::ValueToString() const
    {
        return FormatValue(type_, storage_);
    }

    std::string ICVar::DefaultToString() const
    {
        return FormatValue(type_, defaultStorage_);
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

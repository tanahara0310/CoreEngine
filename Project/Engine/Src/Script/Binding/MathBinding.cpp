#include "pch.h"
#include "Script/Binding/MathBinding.h"

#include "Math/Easing/EasingUtil.h"
#include "Math/MathCore.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <type_traits>

namespace CoreEngine::Script
{
    namespace
    {
        static_assert(sizeof(Vector2) == 8 && std::is_trivially_copyable_v<Vector2>);
        static_assert(sizeof(Vector3) == 12 && std::is_trivially_copyable_v<Vector3>);
        static_assert(sizeof(Vector4) == 16 && std::is_trivially_copyable_v<Vector4>);

        /// @brief 0 で割ろうとしたらスクリプトの例外にする
        /// @return 割ってよければ true
        bool CanDivide(float divisor)
        {
            if (divisor != 0.0f) {
                return true;
            }
            if (asIScriptContext* context = asGetActiveContext()) {
                context->SetException("ベクトルを 0 で割りました");
            }
            return false;
        }

        // ---------------------------------------------------------------- Vector2
        void ConstructVector2(Vector2* self) { new (self) Vector2{ 0.0f, 0.0f }; }
        void ConstructVector2Components(float x, float y, Vector2* self) { new (self) Vector2{ x, y }; }
        Vector2 MultiplyScalarVector2(float scalar, const Vector2& self) { return self * scalar; }
        Vector2 NegateVector2(const Vector2& self) { return -self; }
        Vector2 DivideVector2(float divisor, const Vector2& self)
        {
            return CanDivide(divisor) ? Vector2{ self.x / divisor, self.y / divisor } : self;
        }
        Vector2& DivideAssignVector2(float divisor, Vector2& self)
        {
            if (CanDivide(divisor)) {
                self.x /= divisor;
                self.y /= divisor;
            }
            return self;
        }

        // ---------------------------------------------------------------- Vector3
        void ConstructVector3(Vector3* self) { new (self) Vector3{ 0.0f, 0.0f, 0.0f }; }
        void ConstructVector3Components(float x, float y, float z, Vector3* self) { new (self) Vector3{ x, y, z }; }
        Vector3 MultiplyScalarVector3(float scalar, const Vector3& self) { return self * scalar; }
        Vector3 NegateVector3(const Vector3& self) { return -self; }
        Vector3 DivideVector3(float divisor, const Vector3& self)
        {
            return CanDivide(divisor) ? Vector3{ self.x / divisor, self.y / divisor, self.z / divisor } : self;
        }
        Vector3& DivideAssignVector3(float divisor, Vector3& self)
        {
            if (CanDivide(divisor)) {
                self.x /= divisor;
                self.y /= divisor;
                self.z /= divisor;
            }
            return self;
        }

        // ---------------------------------------------------------------- Vector4
        void ConstructVector4(Vector4* self) { new (self) Vector4{ 0.0f, 0.0f, 0.0f, 0.0f }; }
        void ConstructVector4Components(float x, float y, float z, float w, Vector4* self)
        {
            new (self) Vector4{ x, y, z, w };
        }
        Vector4 MultiplyScalarVector4(float scalar, const Vector4& self) { return self * scalar; }
        Vector4 NegateVector4(const Vector4& self) { return -self; }
        Vector4 DivideVector4(float divisor, const Vector4& self)
        {
            return CanDivide(divisor)
                ? Vector4{ self.x / divisor, self.y / divisor, self.z / divisor, self.w / divisor }
                : self;
        }
        Vector4& DivideAssignVector4(float divisor, Vector4& self)
        {
            if (CanDivide(divisor)) {
                self.x /= divisor;
                self.y /= divisor;
                self.z /= divisor;
                self.w /= divisor;
            }
            return self;
        }

        // ---------------------------------------------------------------- 数値
        int ClampInt(int value, int minValue, int maxValue) { return (std::max)(minValue, (std::min)(maxValue, value)); }
        float MinFloat(float a, float b) { return (std::min)(a, b); }
        float MaxFloat(float a, float b) { return (std::max)(a, b); }
        int MinInt(int a, int b) { return (std::min)(a, b); }
        int MaxInt(int a, int b) { return (std::max)(a, b); }
        float FmodFloat(float x, float y) { return std::fmod(x, y); }
        float ExpFloat(float x) { return std::exp(x); }
        float Exp2Float(float x) { return std::exp2(x); }

        constexpr asQWORD kVectorFlags = asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS;

        void RegisterVector2(BindingRegistrar& r)
        {
            r.ValueType("Vector2", sizeof(Vector2), kVectorFlags | asGetTypeTraits<Vector2>());
            r.Property("Vector2", "float x", asOFFSET(Vector2, x));
            r.Property("Vector2", "float y", asOFFSET(Vector2, y));
            r.Behaviour("Vector2", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructVector2), asCALL_CDECL_OBJLAST);
            r.Behaviour("Vector2", asBEHAVE_CONSTRUCT, "void f(float x, float y)",
                asFUNCTION(ConstructVector2Components), asCALL_CDECL_OBJLAST);
            r.Method("Vector2", "Vector2 opAdd(const Vector2 &in) const",
                asMETHODPR(Vector2, operator+, (const Vector2&) const, Vector2), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 opSub(const Vector2 &in) const",
                asMETHODPR(Vector2, operator-, (const Vector2&) const, Vector2), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 opMul(float) const",
                asMETHODPR(Vector2, operator*, (float) const, Vector2), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 opMul(const Vector2 &in) const",
                asMETHODPR(Vector2, operator*, (const Vector2&) const, Vector2), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 opMul_r(float) const", asFUNCTION(MultiplyScalarVector2), asCALL_CDECL_OBJLAST);
            r.Method("Vector2", "Vector2 opDiv(float) const", asFUNCTION(DivideVector2), asCALL_CDECL_OBJLAST);
            r.Method("Vector2", "Vector2 opNeg() const", asFUNCTION(NegateVector2), asCALL_CDECL_OBJLAST);
            r.Method("Vector2", "Vector2 &opAddAssign(const Vector2 &in)",
                asMETHODPR(Vector2, operator+=, (const Vector2&), Vector2&), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 &opSubAssign(const Vector2 &in)",
                asMETHODPR(Vector2, operator-=, (const Vector2&), Vector2&), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 &opMulAssign(float)",
                asMETHODPR(Vector2, operator*=, (float), Vector2&), asCALL_THISCALL);
            r.Method("Vector2", "Vector2 &opDivAssign(float)", asFUNCTION(DivideAssignVector2), asCALL_CDECL_OBJLAST);
            r.Method("Vector2", "bool opEquals(const Vector2 &in) const",
                asMETHODPR(Vector2, operator==, (const Vector2&) const, bool), asCALL_THISCALL);

            r.Function("float Dot(const Vector2 &in, const Vector2 &in)",
                asFUNCTIONPR(Dot, (const Vector2&, const Vector2&), float));
            r.Function("float Cross(const Vector2 &in, const Vector2 &in)",
                asFUNCTIONPR(Cross, (const Vector2&, const Vector2&), float));
            r.Function("float Length(const Vector2 &in)", asFUNCTIONPR(Length, (const Vector2&), float));
            r.Function("float LengthSquared(const Vector2 &in)", asFUNCTIONPR(LengthSquared, (const Vector2&), float));
            r.Function("float Distance(const Vector2 &in, const Vector2 &in)",
                asFUNCTIONPR(Distance, (const Vector2&, const Vector2&), float));
            r.Function("Vector2 Normalize(const Vector2 &in)", asFUNCTIONPR(Normalize, (const Vector2&), Vector2));
            r.Function("Vector2 Lerp(const Vector2 &in, const Vector2 &in, float)",
                asFUNCTIONPR(MathCore::Lerp, (const Vector2&, const Vector2&, float), Vector2));
        }

        void RegisterVector3(BindingRegistrar& r)
        {
            r.ValueType("Vector3", sizeof(Vector3), kVectorFlags | asGetTypeTraits<Vector3>());
            r.Property("Vector3", "float x", asOFFSET(Vector3, x));
            r.Property("Vector3", "float y", asOFFSET(Vector3, y));
            r.Property("Vector3", "float z", asOFFSET(Vector3, z));
            r.Behaviour("Vector3", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructVector3), asCALL_CDECL_OBJLAST);
            r.Behaviour("Vector3", asBEHAVE_CONSTRUCT, "void f(float x, float y, float z)",
                asFUNCTION(ConstructVector3Components), asCALL_CDECL_OBJLAST);
            r.Method("Vector3", "Vector3 opAdd(const Vector3 &in) const",
                asMETHODPR(Vector3, operator+, (const Vector3&) const, Vector3), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 opSub(const Vector3 &in) const",
                asMETHODPR(Vector3, operator-, (const Vector3&) const, Vector3), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 opMul(float) const",
                asMETHODPR(Vector3, operator*, (float) const, Vector3), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 opMul(const Vector3 &in) const",
                asMETHODPR(Vector3, operator*, (const Vector3&) const, Vector3), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 opMul_r(float) const", asFUNCTION(MultiplyScalarVector3), asCALL_CDECL_OBJLAST);
            r.Method("Vector3", "Vector3 opDiv(float) const", asFUNCTION(DivideVector3), asCALL_CDECL_OBJLAST);
            r.Method("Vector3", "Vector3 opNeg() const", asFUNCTION(NegateVector3), asCALL_CDECL_OBJLAST);
            r.Method("Vector3", "Vector3 &opAddAssign(const Vector3 &in)",
                asMETHODPR(Vector3, operator+=, (const Vector3&), Vector3&), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 &opSubAssign(const Vector3 &in)",
                asMETHODPR(Vector3, operator-=, (const Vector3&), Vector3&), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 &opMulAssign(float)",
                asMETHODPR(Vector3, operator*=, (float), Vector3&), asCALL_THISCALL);
            r.Method("Vector3", "Vector3 &opDivAssign(float)", asFUNCTION(DivideAssignVector3), asCALL_CDECL_OBJLAST);
            r.Method("Vector3", "bool opEquals(const Vector3 &in) const",
                asMETHODPR(Vector3, operator==, (const Vector3&) const, bool), asCALL_THISCALL);

            r.Function("float Dot(const Vector3 &in, const Vector3 &in)",
                asFUNCTIONPR(Dot, (const Vector3&, const Vector3&), float));
            r.Function("Vector3 Cross(const Vector3 &in, const Vector3 &in)",
                asFUNCTIONPR(Cross, (const Vector3&, const Vector3&), Vector3));
            r.Function("float Length(const Vector3 &in)", asFUNCTIONPR(Length, (const Vector3&), float));
            r.Function("float LengthSquared(const Vector3 &in)", asFUNCTIONPR(LengthSquared, (const Vector3&), float));
            r.Function("float Distance(const Vector3 &in, const Vector3 &in)",
                asFUNCTIONPR(Distance, (const Vector3&, const Vector3&), float));
            r.Function("float DistanceSquared(const Vector3 &in, const Vector3 &in)",
                asFUNCTIONPR(DistanceSquared, (const Vector3&, const Vector3&), float));
            r.Function("Vector3 Normalize(const Vector3 &in)", asFUNCTIONPR(Normalize, (const Vector3&), Vector3));
            r.Function("Vector3 Lerp(const Vector3 &in, const Vector3 &in, float)",
                asFUNCTIONPR(MathCore::Lerp, (const Vector3&, const Vector3&, float), Vector3));
            r.Function("Vector3 Clamp(const Vector3 &in, const Vector3 &in, const Vector3 &in)",
                asFUNCTIONPR(MathCore::Clamp, (const Vector3&, const Vector3&, const Vector3&), Vector3));
        }

        void RegisterVector4(BindingRegistrar& r)
        {
            r.ValueType("Vector4", sizeof(Vector4), kVectorFlags | asGetTypeTraits<Vector4>());
            r.Property("Vector4", "float x", asOFFSET(Vector4, x));
            r.Property("Vector4", "float y", asOFFSET(Vector4, y));
            r.Property("Vector4", "float z", asOFFSET(Vector4, z));
            r.Property("Vector4", "float w", asOFFSET(Vector4, w));
            r.Behaviour("Vector4", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructVector4), asCALL_CDECL_OBJLAST);
            r.Behaviour("Vector4", asBEHAVE_CONSTRUCT, "void f(float x, float y, float z, float w)",
                asFUNCTION(ConstructVector4Components), asCALL_CDECL_OBJLAST);
            r.Method("Vector4", "Vector4 opAdd(const Vector4 &in) const",
                asMETHODPR(Vector4, operator+, (const Vector4&) const, Vector4), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 opSub(const Vector4 &in) const",
                asMETHODPR(Vector4, operator-, (const Vector4&) const, Vector4), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 opMul(float) const",
                asMETHODPR(Vector4, operator*, (float) const, Vector4), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 opMul(const Vector4 &in) const",
                asMETHODPR(Vector4, operator*, (const Vector4&) const, Vector4), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 opMul_r(float) const", asFUNCTION(MultiplyScalarVector4), asCALL_CDECL_OBJLAST);
            r.Method("Vector4", "Vector4 opDiv(float) const", asFUNCTION(DivideVector4), asCALL_CDECL_OBJLAST);
            r.Method("Vector4", "Vector4 opNeg() const", asFUNCTION(NegateVector4), asCALL_CDECL_OBJLAST);
            r.Method("Vector4", "Vector4 &opAddAssign(const Vector4 &in)",
                asMETHODPR(Vector4, operator+=, (const Vector4&), Vector4&), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 &opSubAssign(const Vector4 &in)",
                asMETHODPR(Vector4, operator-=, (const Vector4&), Vector4&), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 &opMulAssign(float)",
                asMETHODPR(Vector4, operator*=, (float), Vector4&), asCALL_THISCALL);
            r.Method("Vector4", "Vector4 &opDivAssign(float)", asFUNCTION(DivideAssignVector4), asCALL_CDECL_OBJLAST);
            r.Method("Vector4", "bool opEquals(const Vector4 &in) const",
                asMETHODPR(Vector4, operator==, (const Vector4&) const, bool), asCALL_THISCALL);

            r.Function("float Dot(const Vector4 &in, const Vector4 &in)",
                asFUNCTIONPR(Dot, (const Vector4&, const Vector4&), float));
            r.Function("float Length(const Vector4 &in)", asFUNCTIONPR(Length, (const Vector4&), float));
            r.Function("float LengthSquared(const Vector4 &in)", asFUNCTIONPR(LengthSquared, (const Vector4&), float));
            r.Function("Vector4 Normalize(const Vector4 &in)", asFUNCTIONPR(Normalize, (const Vector4&), Vector4));
            r.Function("Vector4 Lerp(const Vector4 &in, const Vector4 &in, float)",
                asFUNCTIONPR(MathCore::Lerp, (const Vector4&, const Vector4&, float), Vector4));
        }

        void RegisterScalars(BindingRegistrar& r)
        {
            r.Function("float Lerp(float, float, float)", asFUNCTIONPR(MathCore::Lerp, (float, float, float), float));
            r.Function("float Clamp(float, float, float)", asFUNCTIONPR(MathCore::Clamp, (float, float, float), float));
            r.Function("int Clamp(int, int, int)", asFUNCTION(ClampInt));
            r.Function("float Saturate(float)", asFUNCTIONPR(MathCore::Saturate, (float), float));
            r.Function("float Min(float, float)", asFUNCTION(MinFloat));
            r.Function("float Max(float, float)", asFUNCTION(MaxFloat));
            r.Function("int Min(int, int)", asFUNCTION(MinInt));
            r.Function("int Max(int, int)", asFUNCTION(MaxInt));
            r.Function("float fmod(float, float)", asFUNCTION(FmodFloat));
            r.Function("float exp(float)", asFUNCTION(ExpFloat));
            r.Function("float exp2(float)", asFUNCTION(Exp2Float));
        }

        /// @brief 進捗 t にイージングを掛ける（範囲外の種類は t のまま）
        float EaseValue(int type, float t)
        {
            if (type < 0 || type > static_cast<int>(EasingUtil::Type::EaseInOutBounce)) {
                return t;
            }
            return EasingUtil::Apply(t, static_cast<EasingUtil::Type>(type));
        }

        void RegisterEaseType(BindingRegistrar& r)
        {
            r.Enum("EaseType");
            const int last = static_cast<int>(EasingUtil::Type::EaseInOutBounce);
            for (int value = 0; value <= last; ++value) {
                r.EnumValue("EaseType", EasingUtil::GetTypeName(static_cast<EasingUtil::Type>(value)), value);
            }
            r.Function("float Ease(EaseType type, float t)", asFUNCTION(EaseValue));
        }
    }

    bool RegisterMathBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar registrar(engine);
        RegisterVector2(registrar);
        RegisterVector3(registrar);
        RegisterVector4(registrar);
        RegisterScalars(registrar);
        RegisterEaseType(registrar);
        return registrar.Succeeded();
    }
}

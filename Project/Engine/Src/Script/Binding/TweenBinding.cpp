#include "pch.h"
#include "Script/Binding/TweenBinding.h"

#include "GameObject/GameObject.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Script/ScriptCallback.h"
#include "Utility/Tween/Tween.h"

#include <format>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

namespace CoreEngine::Script
{
    namespace
    {
        static_assert(sizeof(TweenHandle) == 8 && std::is_trivially_copyable_v<TweenHandle>);
        static_assert(sizeof(TweenSequence) == 8 && std::is_trivially_copyable_v<TweenSequence>);

        constexpr asQWORD kHandleFlags = asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS;

        /// @brief 止まったときのログに出す、呼び戻しの役割と関数の宣言
        std::string CallbackLabel(asIScriptFunction* function, const char* role)
        {
            if (!function) {
                return role;
            }
            const asIScriptFunction* const target =
                function->GetDelegateFunction() ? function->GetDelegateFunction() : function;
            const char* const declaration = target->GetDeclaration(true, true, false);
            return std::format("{}（{}）", role, declaration ? declaration : "?");
        }

        GameObject* ResolveObject(ScriptGameObject* object)
        {
            return object ? object->Resolve() : nullptr;
        }

        // ---------------------------------------------------------------- TweenHandle

        void ConstructHandle(TweenHandle* self)
        {
            new (self) TweenHandle();
        }

        TweenHandle& HandleSetLink(ScriptGameObject* owner, TweenHandle& self)
        {
            return self.SetLink(ResolveObject(owner));
        }

        TweenHandle& HandleSetId(const std::string& id, TweenHandle& self)
        {
            return self.SetId(id);
        }

        TweenHandle& HandleOnComplete(asIScriptFunction* callback, TweenHandle& self)
        {
            return self.OnComplete(MakeScriptAction(callback, CallbackLabel(callback, "Tween の完了時の関数")));
        }

        TweenHandle& HandleOnStepComplete(asIScriptFunction* callback, TweenHandle& self)
        {
            return self.OnStepComplete(MakeScriptAction(callback, CallbackLabel(callback, "Tween の 1 ループごとの関数")));
        }

        TweenHandle& HandleOnUpdate(asIScriptFunction* callback, TweenHandle& self)
        {
            return self.OnUpdate(MakeScriptFloatAction(callback, CallbackLabel(callback, "Tween の毎フレームの関数")));
        }

        // ---------------------------------------------------------------- TweenSequence

        void ConstructSequence(TweenSequence* self)
        {
            new (self) TweenSequence(TweenHandle());
        }

        TweenSequence& SequenceAppendCallback(asIScriptFunction* action, TweenSequence& self)
        {
            std::function<void()> callback = MakeScriptAction(action, CallbackLabel(action, "シーケンスの途中の関数"));
            return callback ? self.AppendCallback(std::move(callback)) : self;
        }

        TweenSequence& SequenceSetLink(ScriptGameObject* owner, TweenSequence& self)
        {
            return self.SetLink(ResolveObject(owner));
        }

        TweenSequence& SequenceSetId(const std::string& id, TweenSequence& self)
        {
            return self.SetId(id);
        }

        TweenSequence& SequenceOnComplete(asIScriptFunction* callback, TweenSequence& self)
        {
            return self.OnComplete(MakeScriptAction(callback, CallbackLabel(callback, "シーケンスの完了時の関数")));
        }

        // ---------------------------------------------------------------- 生成口

        TweenHandle ToFloat(float from, float to, float duration, asIScriptFunction* setter)
        {
            std::function<void(float)> action = MakeScriptFloatAction(setter, CallbackLabel(setter, "Tween::To の値を受け取る関数"));
            return action ? Tween::To<float>(from, to, duration, std::move(action)) : TweenHandle();
        }

        TweenHandle ToVector2(const Vector2& from, const Vector2& to, float duration, asIScriptFunction* setter)
        {
            auto action = MakeScriptVector2Action(setter, CallbackLabel(setter, "Tween::To の値を受け取る関数"));
            return action ? Tween::To<Vector2>(from, to, duration, std::move(action)) : TweenHandle();
        }

        TweenHandle ToVector3(const Vector3& from, const Vector3& to, float duration, asIScriptFunction* setter)
        {
            auto action = MakeScriptVector3Action(setter, CallbackLabel(setter, "Tween::To の値を受け取る関数"));
            return action ? Tween::To<Vector3>(from, to, duration, std::move(action)) : TweenHandle();
        }

        TweenHandle ToVector4(const Vector4& from, const Vector4& to, float duration, asIScriptFunction* setter)
        {
            auto action = MakeScriptVector4Action(setter, CallbackLabel(setter, "Tween::To の値を受け取る関数"));
            return action ? Tween::To<Vector4>(from, to, duration, std::move(action)) : TweenHandle();
        }

        TweenHandle DelayCall(float seconds, asIScriptFunction* action)
        {
            std::function<void()> callback = MakeScriptAction(action, CallbackLabel(action, "Tween::Delay の関数"));
            return callback ? Tween::Delay(seconds, std::move(callback)) : TweenHandle();
        }

        TweenHandle MoveToObject(ScriptGameObject* object, const Vector3& to, float duration)
        {
            GameObject* const target = ResolveObject(object);
            return target ? Tween::MoveTo(target, to, duration) : TweenHandle();
        }

        TweenHandle ScaleToObject(ScriptGameObject* object, const Vector3& to, float duration)
        {
            GameObject* const target = ResolveObject(object);
            return target ? Tween::ScaleTo(target, to, duration) : TweenHandle();
        }

        TweenHandle RotateToObject(ScriptGameObject* object, const Vector3& to, float duration)
        {
            GameObject* const target = ResolveObject(object);
            return target ? Tween::RotateTo(target, to, duration) : TweenHandle();
        }

        int KillById(const std::string& id, bool complete)
        {
            return Tween::KillById(id, complete);
        }

        int KillByLink(ScriptGameObject* owner, bool complete)
        {
            const GameObject* const target = ResolveObject(owner);
            return target ? Tween::KillByLink(target, complete) : 0;
        }

        TweenSequence MakeSequence()
        {
            return TweenSequence();
        }

        void RegisterEnums(BindingRegistrar& r)
        {
            r.Enum("TweenLoop");
            r.EnumValue("TweenLoop", "Restart", static_cast<int>(TweenLoop::Restart));
            r.EnumValue("TweenLoop", "Yoyo", static_cast<int>(TweenLoop::Yoyo));
            r.Enum("TweenUpdate");
            r.EnumValue("TweenUpdate", "Scaled", static_cast<int>(TweenUpdate::Scaled));
            r.EnumValue("TweenUpdate", "Unscaled", static_cast<int>(TweenUpdate::Unscaled));

            r.Funcdef("void TweenCallback()");
            r.Funcdef("void TweenProgressCallback(float progress)");
            r.Funcdef("void TweenFloatSetter(float value)");
            r.Funcdef("void TweenVector2Setter(const Vector2 &in value)");
            r.Funcdef("void TweenVector3Setter(const Vector3 &in value)");
            r.Funcdef("void TweenVector4Setter(const Vector4 &in value)");
        }

        void RegisterHandle(BindingRegistrar& r)
        {
            r.ValueType("TweenHandle", sizeof(TweenHandle), kHandleFlags | asGetTypeTraits<TweenHandle>());
            r.Behaviour("TweenHandle", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructHandle), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "TweenHandle &SetEase(EaseType ease)",
                asMETHODPR(TweenHandle, SetEase, (EasingUtil::Type), TweenHandle&), asCALL_THISCALL);
            r.Method("TweenHandle", "TweenHandle &SetDelay(float seconds)",
                asMETHODPR(TweenHandle, SetDelay, (float), TweenHandle&), asCALL_THISCALL);
            r.Method("TweenHandle", "TweenHandle &SetLoops(int count, TweenLoop type = TweenLoop::Restart)",
                asMETHODPR(TweenHandle, SetLoops, (int, TweenLoop), TweenHandle&), asCALL_THISCALL);
            r.Method("TweenHandle", "TweenHandle &SetLink(GameObject@+ owner)", asFUNCTION(HandleSetLink), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "TweenHandle &SetUpdateType(TweenUpdate type)",
                asMETHODPR(TweenHandle, SetUpdateType, (TweenUpdate), TweenHandle&), asCALL_THISCALL);
            r.Method("TweenHandle", "TweenHandle &SetId(const string &in id)", asFUNCTION(HandleSetId), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "TweenHandle &OnComplete(TweenCallback@ callback)", asFUNCTION(HandleOnComplete), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "TweenHandle &OnStepComplete(TweenCallback@ callback)",
                asFUNCTION(HandleOnStepComplete), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "TweenHandle &OnUpdate(TweenProgressCallback@ callback)", asFUNCTION(HandleOnUpdate), asCALL_CDECL_OBJLAST);
            r.Method("TweenHandle", "void Kill(bool complete = false)", asMETHOD(TweenHandle, Kill), asCALL_THISCALL);
            r.Method("TweenHandle", "void Complete()", asMETHOD(TweenHandle, Complete), asCALL_THISCALL);
            r.Method("TweenHandle", "bool get_isActive() const property", asMETHOD(TweenHandle, IsActive), asCALL_THISCALL);
            r.Method("TweenHandle", "float get_progress() const property", asMETHOD(TweenHandle, Progress), asCALL_THISCALL);
        }

        void RegisterSequence(BindingRegistrar& r)
        {
            r.ValueType("TweenSequence", sizeof(TweenSequence), kHandleFlags | asGetTypeTraits<TweenSequence>());
            r.Behaviour("TweenSequence", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructSequence), asCALL_CDECL_OBJLAST);
            r.Method("TweenSequence", "TweenSequence &Append(const TweenHandle &in tween)",
                asMETHODPR(TweenSequence, Append, (const TweenHandle&), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &Join(const TweenHandle &in tween)",
                asMETHODPR(TweenSequence, Join, (const TweenHandle&), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &AppendInterval(float seconds)",
                asMETHODPR(TweenSequence, AppendInterval, (float), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &AppendCallback(TweenCallback@ action)",
                asFUNCTION(SequenceAppendCallback), asCALL_CDECL_OBJLAST);
            r.Method("TweenSequence", "TweenSequence &SetLoops(int count, TweenLoop type = TweenLoop::Restart)",
                asMETHODPR(TweenSequence, SetLoops, (int, TweenLoop), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &SetDelay(float seconds)",
                asMETHODPR(TweenSequence, SetDelay, (float), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &SetLink(GameObject@+ owner)", asFUNCTION(SequenceSetLink), asCALL_CDECL_OBJLAST);
            r.Method("TweenSequence", "TweenSequence &SetUpdateType(TweenUpdate type)",
                asMETHODPR(TweenSequence, SetUpdateType, (TweenUpdate), TweenSequence&), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenSequence &SetId(const string &in id)", asFUNCTION(SequenceSetId), asCALL_CDECL_OBJLAST);
            r.Method("TweenSequence", "TweenSequence &OnComplete(TweenCallback@ callback)",
                asFUNCTION(SequenceOnComplete), asCALL_CDECL_OBJLAST);
            r.Method("TweenSequence", "void Kill(bool complete = false)", asMETHOD(TweenSequence, Kill), asCALL_THISCALL);
            r.Method("TweenSequence", "TweenHandle get_handle() const property", asMETHOD(TweenSequence, Handle), asCALL_THISCALL);
        }

        void RegisterFactories(BindingRegistrar& r)
        {
            r.Namespace("Tween");
            r.Function("TweenHandle To(float from, float to, float duration, TweenFloatSetter@ setter)", asFUNCTION(ToFloat));
            r.Function("TweenHandle To(const Vector2 &in from, const Vector2 &in to, float duration, TweenVector2Setter@ setter)",
                asFUNCTION(ToVector2));
            r.Function("TweenHandle To(const Vector3 &in from, const Vector3 &in to, float duration, TweenVector3Setter@ setter)",
                asFUNCTION(ToVector3));
            r.Function("TweenHandle To(const Vector4 &in from, const Vector4 &in to, float duration, TweenVector4Setter@ setter)",
                asFUNCTION(ToVector4));
            r.Function("TweenHandle Delay(float seconds, TweenCallback@ action)", asFUNCTION(DelayCall));
            r.Function("TweenHandle MoveTo(GameObject@+ object, const Vector3 &in to, float duration)", asFUNCTION(MoveToObject));
            r.Function("TweenHandle ScaleTo(GameObject@+ object, const Vector3 &in to, float duration)", asFUNCTION(ScaleToObject));
            r.Function("TweenHandle RotateTo(GameObject@+ object, const Vector3 &in to, float duration)", asFUNCTION(RotateToObject));
            r.Function("int KillById(const string &in id, bool complete = false)", asFUNCTION(KillById));
            r.Function("int KillByLink(GameObject@+ owner, bool complete = false)", asFUNCTION(KillByLink));
            r.Function("TweenSequence Sequence()", asFUNCTION(MakeSequence));
            r.Namespace("");
        }
    }

    bool RegisterTweenBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        RegisterEnums(r);
        RegisterHandle(r);
        RegisterSequence(r);
        RegisterFactories(r);
        return r.Succeeded();
    }
}

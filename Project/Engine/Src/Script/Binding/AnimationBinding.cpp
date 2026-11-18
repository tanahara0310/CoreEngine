#include "pch.h"
#include "Script/Binding/AnimationBinding.h"

#include "Script/Binding/BindingRegistrar.h"

#include <angelscript.h>

namespace CoreEngine::Script
{
    ScriptAnimationEvent* ScriptAnimationEvent::Create(const AnimationEvent& event)
    {
        auto* const created = new ScriptAnimationEvent();
        created->event_ = event;
        return created;
    }

    bool RegisterAnimationBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        r.ReferenceType("AnimationEvent", asOBJ_REF);
        r.RefCountBehaviours<ScriptAnimationEvent>("AnimationEvent");
        r.Method("AnimationEvent", "string get_name() const property", asMETHOD(ScriptAnimationEvent, GetName), asCALL_THISCALL);
        r.Method("AnimationEvent", "string get_clip() const property", asMETHOD(ScriptAnimationEvent, GetClip), asCALL_THISCALL);
        r.Method("AnimationEvent", "string get_state() const property", asMETHOD(ScriptAnimationEvent, GetState), asCALL_THISCALL);
        r.Method("AnimationEvent", "string get_layer() const property", asMETHOD(ScriptAnimationEvent, GetLayer), asCALL_THISCALL);
        r.Method("AnimationEvent", "float get_floatValue() const property", asMETHOD(ScriptAnimationEvent, GetFloat), asCALL_THISCALL);
        r.Method("AnimationEvent", "int get_intValue() const property", asMETHOD(ScriptAnimationEvent, GetInt), asCALL_THISCALL);
        r.Method("AnimationEvent", "string get_stringValue() const property", asMETHOD(ScriptAnimationEvent, GetString), asCALL_THISCALL);
        return r.Succeeded();
    }
}

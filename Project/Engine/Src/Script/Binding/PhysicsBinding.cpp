#include "pch.h"
#include "Script/Binding/PhysicsBinding.h"

#include "Collision/Collider.h"
#include "Collision/CollisionInfo.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::Script
{
    ScriptCollision* ScriptCollision::Create(const CollisionInfo& info, bool trigger)
    {
        auto* const collision = new ScriptCollision();
        collision->other_ = ScriptGameObject::CreateForObject(info.other);
        collision->normal_ = info.normal;
        collision->depth_ = info.depth;
        collision->point_ = info.point;
        collision->layer_ = info.otherCollider ? info.otherCollider->GetLayer() : CollisionLayer::Default;
        collision->selfLayer_ = info.selfCollider ? info.selfCollider->GetLayer() : CollisionLayer::Default;
        collision->trigger_ = trigger;
        return collision;
    }

    ScriptCollision::~ScriptCollision()
    {
        if (other_) {
            other_->Release();
        }
    }

    void ScriptCollision::AddRef() const
    {
        ++refCount_;
    }

    void ScriptCollision::Release() const
    {
        if (--refCount_ == 0) {
            delete this;
        }
    }

    ScriptGameObject* ScriptCollision::GetGameObject() const
    {
        if (other_) {
            other_->AddRef();
        }
        return other_;
    }

    bool RegisterPhysicsBinding(asIScriptEngine* engine)
    {
        BindingRegistrar r(engine);

        r.Enum("CollisionLayer");
        for (std::size_t i = 0; i < std::size(kCollisionLayerNames); ++i) {
            r.EnumValue("CollisionLayer", kCollisionLayerNames[i], static_cast<int>(i));
        }

        r.ReferenceType("Collision", asOBJ_REF);
        r.Behaviour("Collision", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptCollision, AddRef), asCALL_THISCALL);
        r.Behaviour("Collision", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptCollision, Release), asCALL_THISCALL);
        r.Method("Collision", "GameObject@ get_gameObject() const property", asMETHOD(ScriptCollision, GetGameObject), asCALL_THISCALL);
        r.Method("Collision", "Vector3 get_normal() const property", asMETHOD(ScriptCollision, GetNormal), asCALL_THISCALL);
        r.Method("Collision", "float get_depth() const property", asMETHOD(ScriptCollision, GetDepth), asCALL_THISCALL);
        r.Method("Collision", "Vector3 get_point() const property", asMETHOD(ScriptCollision, GetPoint), asCALL_THISCALL);
        r.Method("Collision", "CollisionLayer get_layer() const property", asMETHOD(ScriptCollision, GetLayer), asCALL_THISCALL);
        r.Method("Collision", "CollisionLayer get_selfLayer() const property", asMETHOD(ScriptCollision, GetSelfLayer), asCALL_THISCALL);
        r.Method("Collision", "bool get_isTrigger() const property", asMETHOD(ScriptCollision, IsTrigger), asCALL_THISCALL);

        return r.Succeeded();
    }
}

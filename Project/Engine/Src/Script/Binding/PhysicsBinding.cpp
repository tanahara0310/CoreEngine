#include "pch.h"
#include "Script/Binding/PhysicsBinding.h"

#include "Collision/Collider.h"
#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "Collision/CollisionWorld.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/GameObject.h"
#include "Math/Geometry/Shapes.h"
#include "Scene/Scene.h"
#include "Scene/Feature/CollisionFeature.h"
#include "Scene/SceneManager.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <scriptarray/scriptarray.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace CoreEngine::Script
{
    namespace
    {
        /// 今のシーンを引く先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        /// @brief スクリプトへ渡すレイキャストの当たり（当たったときの値の写しと、相手のハンドル）
        class ScriptRaycastHit
        {
        public:
            explicit ScriptRaycastHit(const RaycastHit& hit)
                : object_(ScriptGameObject::CreateForObject(hit.object)),
                  point_(hit.point),
                  normal_(hit.normal),
                  distance_(hit.distance),
                  layer_(hit.collider ? hit.collider->GetLayer() : CollisionLayer::Default)
            {
            }

            ScriptRaycastHit(const ScriptRaycastHit&) = delete;
            ScriptRaycastHit& operator=(const ScriptRaycastHit&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 当たった GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                if (object_) {
                    object_->AddRef();
                }
                return object_;
            }

            Vector3 GetPoint() const { return point_; }
            Vector3 GetNormal() const { return normal_; }
            float GetDistance() const { return distance_; }
            CollisionLayer GetLayer() const { return layer_; }

        private:
            ~ScriptRaycastHit()
            {
                if (object_) {
                    object_->Release();
                }
            }

            ScriptGameObject* object_ = nullptr;
            Vector3 point_{};
            Vector3 normal_{};
            float distance_ = 0.0f;
            CollisionLayer layer_ = CollisionLayer::Default;
            mutable int refCount_ = 1;
        };

        /// @brief スクリプトへ渡すコライダーのハンドル
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびに ColliderComponent を引き直す。
        ///          読み取りは先頭の形、書き込みはすべての形に効く。
        class ScriptCollider
        {
        public:
            explicit ScriptCollider(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }

            ScriptCollider(const ScriptCollider&) = delete;
            ScriptCollider& operator=(const ScriptCollider&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、ColliderComponent が付いているか
            bool Exists() const { return Find() != nullptr; }

            /// @brief 形の数
            int GetCount() const
            {
                const ColliderComponent* const colliders = Find();
                return colliders ? static_cast<int>(colliders->Count()) : 0;
            }

            bool IsEnabled() const
            {
                const Collider* const first = First();
                return first && first->IsEnabled();
            }

            void SetEnabled(bool enabled)
            {
                ForEach("有効の切り替え", [enabled](Collider& collider) { collider.SetEnabled(enabled); });
            }

            CollisionLayer GetLayer() const
            {
                const Collider* const first = First();
                return first ? first->GetLayer() : CollisionLayer::Default;
            }

            void SetLayer(CollisionLayer layer)
            {
                ForEach("レイヤーの変更", [layer](Collider& collider) { collider.SetLayer(layer); });
            }

            bool IsTrigger() const
            {
                const Collider* const first = First();
                return first && first->IsTrigger();
            }

            void SetTrigger(bool trigger)
            {
                ForEach("トリガーの切り替え", [trigger](Collider& collider) { collider.SetTrigger(trigger); });
            }

            bool IsStatic() const
            {
                const Collider* const first = First();
                return first && first->IsStatic();
            }

            void SetStatic(bool isStatic)
            {
                ForEach("静的の切り替え", [isStatic](Collider& collider) { collider.SetStatic(isStatic); });
            }

            /// @brief 球の形を足す（ColliderComponent が無ければ足す）
            void AddSphere(float radius, CollisionLayer layer, const Vector3& offset)
            {
                if (ColliderComponent* const colliders = FindOrAdd("球の追加")) {
                    colliders->AddSphere(radius, layer, offset);
                }
            }

            /// @brief 箱の形を足す（ColliderComponent が無ければ足す）
            void AddBox(const Vector3& size, CollisionLayer layer, const Vector3& offset)
            {
                if (ColliderComponent* const colliders = FindOrAdd("箱の追加")) {
                    colliders->AddBox(size, layer, offset);
                }
            }

            /// @brief すべての形を取り外す（実体の解放はフレーム末）
            void RemoveAll()
            {
                if (ColliderComponent* const colliders = FindOrWarn("取り外し")) {
                    colliders->RemoveAll();
                }
            }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

        private:
            ~ScriptCollider() { owner_.Release(); }

            ColliderComponent* Find() const
            {
                GameObject* const object = owner_.Resolve();
                return object ? object->GetComponent<ColliderComponent>() : nullptr;
            }

            const Collider* First() const
            {
                const ColliderComponent* const colliders = Find();
                return colliders ? colliders->GetFirst() : nullptr;
            }

            /// @brief 1 回だけ警告を出す
            void WarnOnce(const char* action, const char* reason) const
            {
                if (warned_) {
                    return;
                }
                warned_ = true;
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "Collider で{}をしようとしましたが、{}", action, reason);
            }

            /// @brief ColliderComponent を引き、引けなければ 1 回だけ警告する
            ColliderComponent* FindOrWarn(const char* action) const
            {
                ColliderComponent* const colliders = Find();
                if (!colliders) {
                    WarnOnce(action, "GameObject が無いか Collider が付いていません");
                }
                return colliders;
            }

            /// @brief ColliderComponent を引き、無ければ足す（トランスフォームが無ければ足さずに警告する）
            ColliderComponent* FindOrAdd(const char* action) const
            {
                GameObject* const object = owner_.Resolve();
                if (!object) {
                    WarnOnce(action, "GameObject がありません");
                    return nullptr;
                }
                if (!object->GetComponent<ITransformSource>()) {
                    WarnOnce(action, "トランスフォームが付いていません");
                    return nullptr;
                }
                return object->GetOrAddComponent<ColliderComponent>();
            }

            /// @brief すべての形へ同じ変更をする
            template <class Fn>
            void ForEach(const char* action, Fn&& fn)
            {
                ColliderComponent* const colliders = FindOrWarn(action);
                if (!colliders) {
                    return;
                }
                for (std::size_t i = 0; i < colliders->Count(); ++i) {
                    if (Collider* const collider = colliders->Get(i)) {
                        fn(*collider);
                    }
                }
            }

            ScriptGameObject& owner_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptCollider* GetCollider(ScriptGameObject& self)
        {
            return new ScriptCollider(self);
        }

        /// @brief 今のシーン（読み込みの途中は組み立て中のシーン。無ければ nullptr）
        Scene* FindScene()
        {
            SceneManager* const manager = sEngineSystem ? sEngineSystem->GetSceneManager() : nullptr;
            return manager ? dynamic_cast<Scene*>(manager->GetCurrentScene()) : nullptr;
        }

        /// @brief 今のシーンの当たり判定（シーンが無い・当たり判定を持たないシーンなら nullptr）
        CollisionFeature* FindCollisionFeature()
        {
            Scene* const scene = FindScene();
            return scene ? scene->GetFeature<CollisionFeature>() : nullptr;
        }

        /// @brief 今のシーンの、問い合わせ用の衝突ワールド（シーンが無い・当たり判定を持たないシーンなら nullptr）
        /// @note スクリプトは判定より前（Update）に問い合わせるので、登録をこのフレームのものにしてから使う。
        CollisionWorld* FindQueryWorld()
        {
            Scene* const scene = FindScene();
            CollisionFeature* const feature = scene ? scene->GetFeature<CollisionFeature>() : nullptr;
            GameObjectManager* const objects = scene ? scene->GetGameObjectManager() : nullptr;
            return feature && objects ? &feature->GetQueryWorld(*objects) : nullptr;
        }

        /// @brief スクリプトのレイヤーのビットを、当たり判定のビットへ（-1 はすべてのレイヤー）
        std::uint64_t ToLayerMask(int layerMask)
        {
            return layerMask == -1 ? CollisionWorld::kAllLayers
                                   : static_cast<std::uint64_t>(static_cast<std::uint32_t>(layerMask));
        }

        /// @brief 向きを正規化したレイを作る
        /// @return 向きの長さが 0 なら false
        bool MakeRay(const Vector3& origin, const Vector3& direction, Geometry::Ray& out)
        {
            const float length = Length(direction);
            if (length <= 1.0e-6f) {
                return false;
            }
            out = Geometry::Ray(origin, direction / length);
            return true;
        }

        /// @brief 要素の型を指定した空の配列を作る（スクリプトの中から呼ばれたときだけ作れる）
        CScriptArray* CreateArray(const char* declaration)
        {
            asIScriptContext* const context = asGetActiveContext();
            asIScriptEngine* const engine = context ? context->GetEngine() : nullptr;
            asITypeInfo* const type = engine ? engine->GetTypeInfoByDecl(declaration) : nullptr;
            return type ? CScriptArray::Create(type) : nullptr;
        }

        ScriptRaycastHit* Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, int layerMask)
        {
            CollisionWorld* const world = FindQueryWorld();
            Geometry::Ray ray;
            if (!world || maxDistance <= 0.0f || !MakeRay(origin, direction, ray)) {
                return nullptr;
            }
            RaycastHit hit;
            if (!world->Raycast(ray, maxDistance, ToLayerMask(layerMask), &hit)) {
                return nullptr;
            }
            return new ScriptRaycastHit(hit);
        }

        CScriptArray* RaycastAll(const Vector3& origin, const Vector3& direction, float maxDistance, int layerMask)
        {
            CScriptArray* const result = CreateArray("array<RaycastHit@>");
            CollisionWorld* const world = FindQueryWorld();
            Geometry::Ray ray;
            if (!result || !world || maxDistance <= 0.0f || !MakeRay(origin, direction, ray)) {
                return result;
            }
            std::vector<RaycastHit> hits;
            world->RaycastAll(ray, maxDistance, ToLayerMask(layerMask), hits);
            for (const RaycastHit& hit : hits) {
                ScriptRaycastHit* handle = new ScriptRaycastHit(hit);
                result->InsertLast(&handle);
                handle->Release();
            }
            return result;
        }

        /// @brief 重なったコライダーの持ち主を、同じものが重ならないように配列へ入れる
        CScriptArray* ToObjectArray(const std::vector<Collider*>& colliders)
        {
            CScriptArray* const result = CreateArray("array<GameObject@>");
            if (!result) {
                return nullptr;
            }
            std::vector<const GameObject*> seen;
            for (const Collider* const collider : colliders) {
                const GameObject* const object = collider ? collider->GetOwner() : nullptr;
                if (!object || std::find(seen.begin(), seen.end(), object) != seen.end()) {
                    continue;
                }
                seen.push_back(object);
                ScriptGameObject* handle = ScriptGameObject::CreateForObject(object);
                if (handle) {
                    result->InsertLast(&handle);
                    handle->Release();
                }
            }
            return result;
        }

        CScriptArray* OverlapSphere(const Vector3& center, float radius, int layerMask)
        {
            std::vector<Collider*> colliders;
            if (CollisionWorld* const world = FindQueryWorld(); world && radius > 0.0f) {
                world->OverlapSphere(Geometry::Sphere(center, radius), ToLayerMask(layerMask), colliders);
            }
            return ToObjectArray(colliders);
        }

        CScriptArray* OverlapBox(const Vector3& center, const Vector3& size, int layerMask)
        {
            std::vector<Collider*> colliders;
            if (CollisionWorld* const world = FindQueryWorld()) {
                const Vector3 half = size * 0.5f;
                world->OverlapBox(Geometry::AABB(center - half, center + half), ToLayerMask(layerMask), colliders);
            }
            return ToObjectArray(colliders);
        }

        void SetLayerCollision(CollisionLayer a, CollisionLayer b, bool enabled)
        {
            if (CollisionFeature* const feature = FindCollisionFeature()) {
                feature->SetCollisionEnabled(a, b, enabled);
            }
        }

        bool GetLayerCollision(CollisionLayer a, CollisionLayer b)
        {
            CollisionFeature* const feature = FindCollisionFeature();
            return feature && feature->GetConfig().IsCollisionEnabled(a, b);
        }

        int LayerMask(CollisionLayer layer)
        {
            return 1 << static_cast<int>(layer);
        }
    }

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

    bool RegisterPhysicsBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        sEngineSystem = engineSystem;
        BindingRegistrar r(engine);

        // 名前はプロジェクト設定が持つので、スクリプトの列挙もそこから作る
        r.Enum("CollisionLayer");
        const std::vector<std::string>& layerNames = CollisionLayers::Names();
        for (std::size_t i = 0; i < layerNames.size(); ++i) {
            r.EnumValue("CollisionLayer", layerNames[i].c_str(), static_cast<int>(i));
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

        r.ReferenceType("RaycastHit", asOBJ_REF);
        r.Behaviour("RaycastHit", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptRaycastHit, AddRef), asCALL_THISCALL);
        r.Behaviour("RaycastHit", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptRaycastHit, Release), asCALL_THISCALL);
        r.Method("RaycastHit", "GameObject@ get_gameObject() const property", asMETHOD(ScriptRaycastHit, GetGameObject), asCALL_THISCALL);
        r.Method("RaycastHit", "Vector3 get_point() const property", asMETHOD(ScriptRaycastHit, GetPoint), asCALL_THISCALL);
        r.Method("RaycastHit", "Vector3 get_normal() const property", asMETHOD(ScriptRaycastHit, GetNormal), asCALL_THISCALL);
        r.Method("RaycastHit", "float get_distance() const property", asMETHOD(ScriptRaycastHit, GetDistance), asCALL_THISCALL);
        r.Method("RaycastHit", "CollisionLayer get_layer() const property", asMETHOD(ScriptRaycastHit, GetLayer), asCALL_THISCALL);

        r.ReferenceType("Collider", asOBJ_REF);
        r.Behaviour("Collider", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptCollider, AddRef), asCALL_THISCALL);
        r.Behaviour("Collider", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptCollider, Release), asCALL_THISCALL);
        r.Method("Collider", "bool get_exists() const property", asMETHOD(ScriptCollider, Exists), asCALL_THISCALL);
        r.Method("Collider", "int get_count() const property", asMETHOD(ScriptCollider, GetCount), asCALL_THISCALL);
        r.Method("Collider", "bool get_enabled() const property", asMETHOD(ScriptCollider, IsEnabled), asCALL_THISCALL);
        r.Method("Collider", "void set_enabled(bool) property", asMETHOD(ScriptCollider, SetEnabled), asCALL_THISCALL);
        r.Method("Collider", "CollisionLayer get_layer() const property", asMETHOD(ScriptCollider, GetLayer), asCALL_THISCALL);
        r.Method("Collider", "void set_layer(CollisionLayer) property", asMETHOD(ScriptCollider, SetLayer), asCALL_THISCALL);
        r.Method("Collider", "bool get_isTrigger() const property", asMETHOD(ScriptCollider, IsTrigger), asCALL_THISCALL);
        r.Method("Collider", "void set_isTrigger(bool) property", asMETHOD(ScriptCollider, SetTrigger), asCALL_THISCALL);
        r.Method("Collider", "bool get_isStatic() const property", asMETHOD(ScriptCollider, IsStatic), asCALL_THISCALL);
        r.Method("Collider", "void set_isStatic(bool) property", asMETHOD(ScriptCollider, SetStatic), asCALL_THISCALL);
        r.Method("Collider", "void AddSphere(float radius, CollisionLayer layer = CollisionLayer::Default, const Vector3&in offset = Vector3())",
            asMETHOD(ScriptCollider, AddSphere), asCALL_THISCALL);
        r.Method("Collider", "void AddBox(const Vector3&in size, CollisionLayer layer = CollisionLayer::Default, const Vector3&in offset = Vector3())",
            asMETHOD(ScriptCollider, AddBox), asCALL_THISCALL);
        r.Method("Collider", "void RemoveAll()", asMETHOD(ScriptCollider, RemoveAll), asCALL_THISCALL);
        r.Method("Collider", "GameObject@ get_gameObject() const property", asMETHOD(ScriptCollider, GetGameObject), asCALL_THISCALL);
        r.Method("GameObject", "Collider@ get_collider() property", asFUNCTION(GetCollider), asCALL_CDECL_OBJLAST);

        r.Namespace("Physics");
        r.Function("RaycastHit@ Raycast(const Vector3&in origin, const Vector3&in direction, float maxDistance, int layerMask = -1)",
            asFUNCTION(Raycast));
        r.Function("array<RaycastHit@>@ RaycastAll(const Vector3&in origin, const Vector3&in direction, float maxDistance, int layerMask = -1)",
            asFUNCTION(RaycastAll));
        r.Function("array<GameObject@>@ OverlapSphere(const Vector3&in center, float radius, int layerMask = -1)",
            asFUNCTION(OverlapSphere));
        r.Function("array<GameObject@>@ OverlapBox(const Vector3&in center, const Vector3&in size, int layerMask = -1)",
            asFUNCTION(OverlapBox));
        r.Function("void SetLayerCollision(CollisionLayer a, CollisionLayer b, bool enabled)", asFUNCTION(SetLayerCollision));
        r.Function("bool GetLayerCollision(CollisionLayer a, CollisionLayer b)", asFUNCTION(GetLayerCollision));
        r.Function("int LayerMask(CollisionLayer layer)", asFUNCTION(LayerMask));
        r.Namespace("");

        return r.Succeeded();
    }
}

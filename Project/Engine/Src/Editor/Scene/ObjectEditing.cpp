#include "pch.h"
#include "Editor/Scene/ObjectEditing.h"

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Particle/Gpu/GpuParticleSystemComponent.h"
#include "Particle/ParticleSystemComponent.h"
#include "Scene/PrefabSystem.h"
#include "UI/RectTransformComponent.h"
#include "UI/UIImageComponent.h"
#include "UI/UITextComponent.h"
#include "Utility/Logger/Logger.h"

#include <cctype>
#include <memory>
#include <utility>

namespace CoreEngine::ObjectEditing
{
    namespace
    {
        /// 空のオブジェクトの名前
        constexpr const char* kEmptyObjectName = "GameObject";

        /// パーティクルのオブジェクトの名前
        constexpr const char* kParticleObjectName = "Particle System";

        /// GPU パーティクルのオブジェクトの名前
        constexpr const char* kGpuParticleObjectName = "GPU Particle System";

        /// UI テキストのオブジェクトの名前
        constexpr const char* kTextObjectName = "Text";

        /// UI 画像のオブジェクトの名前
        constexpr const char* kImageObjectName = "Image";

        /// 作った UI テキストの文字列
        constexpr const char* kNewTextString = "新しいテキスト";

        /// UI を複製したときにずらす量（px）
        constexpr float kUIDuplicateOffset = 10.0f;

        /// @brief オブジェクトを作り直すための控え
        struct Snapshot
        {
            std::string name;
            std::string serializeKey;
            ObjectId id;
            json state;
            Reflection::AssetRefValue prefab;
        };

        /// @brief 今の状態を控える
        Snapshot Capture(const GameObject& object)
        {
            Snapshot snapshot;
            snapshot.name = object.GetName();
            snapshot.serializeKey = object.GetSerializeKey();
            snapshot.id = object.GetObjectId();
            snapshot.state = object.Serialize();
            if (object.IsPrefabInstance()) {
                snapshot.prefab = object.GetPrefab().GetValue();
            }
            return snapshot;
        }

        /// @brief 同じ名前のオブジェクトがあるか
        bool HasObjectNamed(const GameObjectManager& manager, const std::string& name)
        {
            for (const auto& object : manager.GetAllObjects()) {
                if (object && !object->IsMarkedForDestroy() && object->GetName() == name) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 「名前 (n)」の形なら、(n) を除いた名前を返す
        std::string StripCopySuffix(const std::string& name)
        {
            if (name.size() < 4 || name.back() != ')') {
                return name;
            }
            const std::size_t open = name.rfind(" (");
            if (open == std::string::npos || open + 3 >= name.size()) {
                return name;
            }
            for (std::size_t i = open + 2; i + 1 < name.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
                    return name;
                }
            }
            return name.substr(0, open);
        }

        /// @brief 使われていない「名前 (n)」を作る
        std::string MakeCopyName(const GameObjectManager& manager, const std::string& sourceName)
        {
            const std::string baseName = StripCopySuffix(sourceName);
            for (int index = 1;; ++index) {
                std::string candidate = baseName + " (" + std::to_string(index) + ")";
                if (!HasObjectNamed(manager, candidate)) {
                    return candidate;
                }
            }
        }

        /// @brief 控えからオブジェクトを作ってシーンへ置く
        /// @param keepIdentity 控えた ID と保存キーで置くか（消したものを戻すとき）
        GameObject* Recreate(GameObjectManager& manager, const Snapshot& snapshot, const std::string& name,
                             bool keepIdentity)
        {
            GameObject* object = nullptr;
            if (!snapshot.prefab.guid.empty() || !snapshot.prefab.path.empty()) {
                object = PrefabSystem::Instantiate(manager, snapshot.prefab, name);
            } else {
                auto owned = std::make_unique<GameObject>();
                owned->SetName(name);
                object = manager.AddObject(std::move(owned));
            }
            if (!object) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "ObjectEditing: \"{}\" を作り直せませんでした", name);
                return nullptr;
            }

            if (keepIdentity) {
                object->SetSerializeKey(snapshot.serializeKey);
                manager.AssignObjectId(*object, snapshot.id);
            }
            object->Deserialize(snapshot.state);
            object->SetName(name);
            manager.InvalidateReferences();
            return object;
        }

        /// @brief 後始末を呼んでから、削除の印を付ける
        void DestroyObject(const Context& context, GameObject& object)
        {
            if (context.beforeDestroy) {
                context.beforeDestroy(object);
            }
            object.Destroy();
            context.manager->InvalidateReferences();
        }

        /// @brief 作ったオブジェクトを、Undo で消し Redo で同じ ID のまま作り直すコマンドを積む
        void PushCreateCommand(const Context& context, const GameObject& object, std::string label)
        {
            Snapshot snapshot = Capture(object);
            const ObjectId id = snapshot.id;
            Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                std::move(label),
                [context, id] {
                    if (GameObject* const target = context.manager->FindObject(id)) {
                        DestroyObject(context, *target);
                    }
                },
                [context, snapshot = std::move(snapshot)] {
                    Recreate(*context.manager, snapshot, snapshot.name, true);
                }));
        }
    }

    bool CanDuplicateOrDelete(const GameObject& object, std::string* reason)
    {
        const auto fail = [reason](const char* message) {
            if (reason) {
                *reason = message;
            }
            return false;
        };

        if (object.IsMarkedForDestroy()) {
            return fail("削除の途中のオブジェクトです");
        }
        if (!object.IsSerializeEnabled()) {
            return fail("シーンに保存しないオブジェクトは扱えません");
        }
        for (const auto& component : object.GetAllComponents()) {
            if (component && component->IsAttachedByCode()) {
                return fail("コードが作ったオブジェクトは、エディタから複製・削除できません");
            }
        }
        return true;
    }

    GameObject* CreateEmpty(const Context& context, const Vector3& position)
    {
        GameObjectManager& manager = *context.manager;
        auto owned = std::make_unique<GameObject>();
        owned->SetName(HasObjectNamed(manager, kEmptyObjectName)
            ? MakeCopyName(manager, kEmptyObjectName) : std::string(kEmptyObjectName));
        GameObject* const object = manager.AddObject(std::move(owned));
        if (!object) {
            return nullptr;
        }

        // エディタが作るオブジェクトのコンポーネントとして付ける
        {
            ComponentHost::DataAttachScope dataScope(*object);
            if (TransformComponent* const transform = object->AddComponent<TransformComponent>()) {
                transform->Get().translate = position;
            }
        }
        manager.InvalidateReferences();

        PushCreateCommand(context, *object, object->GetName() + " を作る");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: 空のオブジェクト \"{}\" を作りました", object->GetName());
        return object;
    }

    GameObject* CreateParticle(const Context& context, ParticleKind kind, const Vector3& position)
    {
        GameObjectManager& manager = *context.manager;
        const char* const baseName = (kind == ParticleKind::Gpu) ? kGpuParticleObjectName : kParticleObjectName;
        auto owned = std::make_unique<GameObject>();
        owned->SetName(HasObjectNamed(manager, baseName)
            ? MakeCopyName(manager, baseName) : std::string(baseName));
        GameObject* const object = manager.AddObject(std::move(owned));
        if (!object) {
            return nullptr;
        }

        // エディタが作るオブジェクトのコンポーネントとして、トランスフォームを先頭に付ける
        {
            ComponentHost::DataAttachScope dataScope(*object);
            if (TransformComponent* const transform = object->AddComponent<TransformComponent>()) {
                transform->Get().translate = position;
            }
            if (kind == ParticleKind::Gpu) {
                object->AddComponent<GpuParticleSystemComponent>();
            } else {
                object->AddComponent<ParticleSystemComponent>();
            }
        }
        manager.InvalidateReferences();

        PushCreateCommand(context, *object, object->GetName() + " を作る");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: パーティクルのオブジェクト \"{}\" を作りました", object->GetName());
        return object;
    }

    GameObject* CreateUI(const Context& context, UIElementKind kind)
    {
        GameObjectManager& manager = *context.manager;
        const char* const baseName = (kind == UIElementKind::Text) ? kTextObjectName : kImageObjectName;
        auto owned = std::make_unique<GameObject>();
        owned->SetName(HasObjectNamed(manager, baseName)
            ? MakeCopyName(manager, baseName) : std::string(baseName));
        GameObject* const object = manager.AddObject(std::move(owned));
        if (!object) {
            return nullptr;
        }

        // エディタが作るオブジェクトのコンポーネントとして、UI トランスフォームを先頭に付ける
        {
            ComponentHost::DataAttachScope dataScope(*object);
            object->AddComponent<RectTransformComponent>();
            if (kind == UIElementKind::Text) {
                if (UITextComponent* const text = object->AddComponent<UITextComponent>()) {
                    text->SetText(kNewTextString);
                }
            } else {
                object->AddComponent<UIImageComponent>();
            }
        }
        manager.InvalidateReferences();

        PushCreateCommand(context, *object, object->GetName() + " を作る");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: UI のオブジェクト \"{}\" を作りました", object->GetName());
        return object;
    }

    GameObject* Duplicate(const Context& context, const GameObject& source)
    {
        std::string reason;
        if (!CanDuplicateOrDelete(source, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectEditing: \"{}\" を複製できません: {}", source.GetName(), reason);
            return nullptr;
        }

        GameObjectManager& manager = *context.manager;
        const Snapshot snapshot = Capture(source);
        GameObject* const copy = Recreate(manager, snapshot, MakeCopyName(manager, source.GetName()), false);
        if (!copy) {
            return nullptr;
        }

        // 元と重ならないよう少しずらす
        if (ITransformSource* const transform = copy->GetComponent<ITransformSource>()) {
            transform->Translate().x += 1.0f;
        } else if (RectTransformComponent* const rect = copy->GetComponent<RectTransformComponent>()) {
            const Vector2 position = rect->GetAnchoredPosition();
            rect->SetAnchoredPosition({ position.x + kUIDuplicateOffset, position.y + kUIDuplicateOffset });
        }

        PushCreateCommand(context, *copy, source.GetName() + " を複製");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: \"{}\" を \"{}\" として複製しました", source.GetName(), copy->GetName());
        return copy;
    }

    bool Delete(const Context& context, GameObject& object)
    {
        std::string reason;
        if (!CanDuplicateOrDelete(object, &reason)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ObjectEditing: \"{}\" を削除できません: {}", object.GetName(), reason);
            return false;
        }

        Snapshot snapshot = Capture(object);
        const ObjectId id = snapshot.id;
        const std::string name = snapshot.name;
        DestroyObject(context, object);

        Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
            name + " を削除",
            [context, snapshot = std::move(snapshot)] {
                Recreate(*context.manager, snapshot, snapshot.name, true);
            },
            [context, id] {
                if (GameObject* const target = context.manager->FindObject(id)) {
                    DestroyObject(context, *target);
                }
            }));

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "ObjectEditing: \"{}\" を削除しました", name);
        return true;
    }
}

#endif // USE_IMGUI

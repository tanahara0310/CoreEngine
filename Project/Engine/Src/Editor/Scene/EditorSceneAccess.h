#pragma once

#ifdef USE_IMGUI

#include "Editor/Command/EditorCommand.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/ObjectId.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace CoreEngine
{
    class EngineSystem;
    class GameObject;
    class GameObjectManager;
    class SceneDebugEditor;
}

namespace CoreEngine::Editor
{
    /// @brief Undo / Redo が、今開いているシーンを引く口
    namespace SceneAccess
    {
        /// @brief 引く先のエンジンを設定する（nullptr で外す）
        void Bind(EngineSystem* engine);

        /// @brief 今のシーンのオブジェクト（シーンが無ければ nullptr）
        GameObjectManager* Objects();

        /// @brief 今のシーンのエディタ（無ければ nullptr）
        SceneDebugEditor* SceneEditor();

        /// @brief 今のシーンから ID でオブジェクトを引く（無ければ nullptr）
        GameObject* FindObject(ObjectId id);

        /// @brief オブジェクトを選んでいたら選択を外す
        void Deselect(const GameObject& object);
    }

    /// @brief シーンを組み直しても同じコンポーネントを指す目印
    /// @details オブジェクトの ID と、型名が同じコンポーネントの中での順番で引く。
    struct ComponentHandle
    {
        ObjectId object{};
        std::string type;
        std::size_t ordinal = 0;

        /// @brief 付いているコンポーネントの目印を作る
        static ComponentHandle Of(const IComponent& component);

        /// @brief 今のシーンから引く（無ければ nullptr）
        IComponent* Resolve() const;
    };

    /// @brief 状態まるごとで戻す操作のうち、相手のコンポーネントを今のシーンから引き直すもの
    /// @tparam TComponent 相手のコンポーネントの型
    /// @tparam TState 状態の型（コピー可能であること）
    /// @note 変更した後の状態は、最初に戻すときに控える。
    template <class TComponent, class TState>
    class ComponentStateCommand final : public IEditorCommand
    {
    public:
        using Capture = std::function<TState(TComponent&)>;
        using Restore = std::function<void(TComponent&, const TState&)>;

        ComponentStateCommand(std::string label, const TComponent& component, TState before,
                              Capture capture, Restore restore)
            : label_(std::move(label)), handle_(ComponentHandle::Of(component)), before_(std::move(before)),
              capture_(std::move(capture)), restore_(std::move(restore)) {}

        void Undo() override
        {
            TComponent* const target = Resolve();
            if (!target) { return; }
            after_ = capture_(*target);
            hasAfter_ = true;
            restore_(*target, before_);
        }

        void Redo() override
        {
            TComponent* const target = Resolve();
            if (!target || !hasAfter_) { return; }
            restore_(*target, after_);
        }

        std::string GetLabel() const override { return label_; }
        bool SurvivesSceneReload() const override { return true; }

    private:
        TComponent* Resolve() const { return dynamic_cast<TComponent*>(handle_.Resolve()); }

        std::string label_;
        ComponentHandle handle_;
        TState before_{};
        TState after_{};
        bool hasAfter_ = false;
        Capture capture_;
        Restore restore_;
    };
}

#endif // USE_IMGUI

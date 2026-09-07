#pragma once

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include "UI/UIElement.h"
#include "Math/Vector/Vector2.h"
#include <string>
#include <vector>

namespace CoreEngine
{
    class EngineSystem;
    class GameObjectManager;
    class TextureManager;
    class UIImage;
    class UIText;
    class GameObject;
    class SceneDebugEditor;

    /// @brief UI を配置するための ImGui ウィンドウ（Unity の Scene ビュー相当）
    /// @details
    ///  **背景にはゲームの描画結果そのもの**を敷き、その上へ選択枠とギズモだけを重ねる。
    ///  ImGui の DrawList で UI を模写すると、MSDF テキストの縁取り・回転・字形を
    ///  再現できず「Game ビューに戻らないと結果が分からない」状態になるため。
    ///  背景が渡らなかった場合だけ、従来どおり DrawList による簡易表示へ落ちる。
    class CanvasViewport
    {
    public:
        CanvasViewport()  = default;
        ~CanvasViewport() = default;

        /// @brief 初期化（背景テクスチャを読み込む）
        /// @param engine EngineSystem ポインタ（GameObjectManager 取得用）
        void Initialize(EngineSystem* engine);

        /// @brief Canvas ウィンドウの描画
        /// @param gameTextureHandlePtr
        ///  ゲームの最終描画結果の SRV（D3D12_GPU_DESCRIPTOR_HANDLE::ptr）。
        ///  0 を渡すと DrawList による簡易表示へ落ちる
        /// @param sceneDebugEditor
        ///  選択状態を Hierarchy / Inspector と共有するための参照。
        ///  nullptr なら Canvas 内だけで選択を持つ
        void DrawCanvasViewport(unsigned long long gameTextureHandlePtr = 0,
            SceneDebugEditor* sceneDebugEditor = nullptr);

    private:
        /// @brief Canvas 上で編集できる UI 要素
        /// @details UIImage と UIText を同じ扱いにするための薄い受け皿。
        ///          どちらか一方のポインタだけが入る
        struct CanvasElement
        {
            GameObject* object = nullptr;
            UIImage* image = nullptr;
            UIText* text = nullptr;

            const UILayout& Layout() const;
            Vector2 Pivot() const;
            UIAnchor Anchor() const;
            void SetAnchoredPosition(const Vector2& position) const;
            void SetRotation(float radians) const;
            /// @brief 矩形の大きさを変える
            /// @note テキストの場合はテキストフィールドの大きさになる
            void SetSize(const Vector2& size) const;

            bool IsText() const { return text != nullptr; }
        };

        /// @brief Edit モードの選択とギズモ操作
        /// @details
        ///  移動・回転・拡縮は 3D オブジェクトやスプライトと同じ ImGuizmo で行う。
        ///  Canvas 専用のハンドルを持たせると、
        ///  ・操作モード（W/E/R とツールバー）が UI だけ効かない
        ///  ・掴み方が Game ビューと違う
        ///  という食い違いが出るため、操作系は共通のギズモへ寄せている。
        void HandleGizmoInteraction(
            const std::vector<CanvasElement>& elements,
            const ImVec2& canvasMin,
            const ImVec2& canvasSize,
            const Vector2& referenceSize,
            const Vector2& scale,
            const ImVec2& mousePos,
            bool mouseInCanvas,
            ImDrawList* drawList);

    private:
        EngineSystem* engine_ = nullptr;

        /// @brief 背景テクスチャの GPU ハンドル
        unsigned long long backgroundTextureHandlePtr_ = 0;
        Vector2 backgroundTextureSize_ = { 0.0f, 0.0f };

        /// @brief 現在選択中の要素を elements から引き直す（破棄済みなら nullptr）
        const CanvasElement* FindSelected(const std::vector<CanvasElement>& elements) const;

        /// @brief 「テキストを追加」ボタン。Canvas 中央に UIText を 1 つ作る
        void DrawCreateUI(GameObjectManager* gom);

        /// @brief 矢印キーで選択中の要素を 1px（Shift で 10px）動かす
        /// @details マウスドラッグだけだと 1px 単位の追い込みができないため
        void HandleKeyboardNudge(const std::vector<CanvasElement>& elements);

        /// @brief 選択中のオブジェクトを取得する
        /// @details SceneDebugEditor があればそちらの選択を正とする。
        ///          Canvas で掴んだ要素がそのまま Inspector に出るようにするため
        GameObject* GetSelection() const;
        /// @brief オブジェクトを選択する
        void SetSelection(GameObject* object);

        // Edit モード（ギズモ操作）
        bool editMode_ = false;
        /// 選択状態の共有先。null なら Canvas 内で完結する
        SceneDebugEditor* sceneDebugEditor_ = nullptr;
        /// 共有先が無いときに使う選択（UIImage / UIText のどちらか）
        GameObject* selectedObject_ = nullptr;
    };

} // namespace CoreEngine

#endif // USE_IMGUI

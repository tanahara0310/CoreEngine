#include "pch.h"
#include "UI/UIPointer.h"

#include "Graphics/Render/GameOutputWindow.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "WinApp/WinApp.h"

#include <Windows.h>

namespace CoreEngine
{
    namespace
    {
        /// @brief 窓のクライアント領域を画面座標で取る（取れなければ false）
        bool GetClientRectOnScreen(HWND hwnd, Vector2& outOrigin, Vector2& outSize)
        {
            if (!hwnd || !IsWindow(hwnd)) {
                return false;
            }
            RECT client{};
            if (!GetClientRect(hwnd, &client)) {
                return false;
            }
            POINT origin{ client.left, client.top };
            if (!ClientToScreen(hwnd, &origin)) {
                return false;
            }
            const float width = static_cast<float>(client.right - client.left);
            const float height = static_cast<float>(client.bottom - client.top);
            if (width <= 0.0f || height <= 0.0f) {
                return false;
            }
            outOrigin = { static_cast<float>(origin.x), static_cast<float>(origin.y) };
            outSize = { width, height };
            return true;
        }
    }

    UIPointer& UIPointer::Get()
    {
        static UIPointer instance;
        return instance;
    }

    void UIPointer::SetViewRect(const Vector2& origin, const Vector2& size, bool acceptsPointer)
    {
        viewOrigin_ = origin;
        viewSize_ = size;
        viewAccepts_ = acceptsPointer;
        hasViewRect_ = true;
    }

    bool UIPointer::ConsumeViewRect(Vector2& outOrigin, Vector2& outSize, bool& outAccepts)
    {
        if (!hasViewRect_) {
            return false;
        }
        outOrigin = viewOrigin_;
        outSize = viewSize_;
        outAccepts = viewAccepts_;
        // 1 フレーム分だけ有効。教わらなくなったら窓へ落ちる
        hasViewRect_ = false;
        return true;
    }

    void UIPointer::Update([[maybe_unused]] const WinApp* winApp, const InputManager* input,
                           const GameOutputWindow* outputWindow)
    {
        normalized_ = {};
        isOver_ = false;
        pressed_ = false;
        held_ = false;
        released_ = false;

        if (!input) {
            return;
        }
        const InputQuery& query = input->GetQuery();

        Vector2 origin{};
        Vector2 size{};
        bool accepts = true;

        // ゲーム映像専用ウィンドウが開いていればそちらが本命。
        // 無ければエディタが教えた Game ビュー、それも無ければ本体ウィンドウ
        const HWND outputHwnd = outputWindow ? outputWindow->GetHwnd() : nullptr;
        if (outputHwnd && GetClientRectOnScreen(outputHwnd, origin, size)) {
            // 教わった矩形は使わないが、溜め込まないように捨てる
            Vector2 unusedOrigin{};
            Vector2 unusedSize{};
            bool unusedAccepts = false;
            ConsumeViewRect(unusedOrigin, unusedSize, unusedAccepts);
        } else if (!ConsumeViewRect(origin, size, accepts)) {
#ifdef CORE_EDITOR
            // エディタは Game ビューが描かれたときだけ教えてくる。
            // 閉じている間に本体ウィンドウへ落とすと、エディタのどこを押しても
            // ゲームの UI に当たってしまう
            return;
#else
            if (!winApp || !GetClientRectOnScreen(winApp->GetHwnd(), origin, size)) {
                return;
            }
#endif
        }

        if (size.x <= 0.0f || size.y <= 0.0f) {
            return;
        }

        const POINT cursor = query.GetCursorPosition();
        const Vector2 local{ static_cast<float>(cursor.x) - origin.x,
                             static_cast<float>(cursor.y) - origin.y };
        normalized_ = { local.x / size.x, local.y / size.y };

        const bool inside = normalized_.x >= 0.0f && normalized_.x <= 1.0f
                         && normalized_.y >= 0.0f && normalized_.y <= 1.0f;
        isOver_ = inside && accepts;

        // 押している間は画像の外へ出ても離すまで追いかける（掴んだまま外して戻る操作のため）
        held_ = query.IsMouseButtonPressed(MouseButton::Left);
        pressed_ = isOver_ && query.IsMouseButtonTriggered(MouseButton::Left);
        released_ = query.IsMouseButtonReleased(MouseButton::Left);
    }
}

#pragma once

#ifdef CORE_EDITOR

#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector3.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class Camera;
    class GameObject;
    class GameObjectManager;
}

namespace CoreEngine::Editor
{
    /// @brief シーンビュー（エディタの Game ウィンドウに映したシーン）の 1 フレーム分の情報
    struct SceneViewContext
    {
        /// 画像の左上と大きさ（ImGui の画面座標）
        ImVec2 position{};
        ImVec2 size{};

        /// カーソルが画像の上にあるか（ほかのウィンドウに隠れていない）
        bool hovered = false;

        const Camera* camera = nullptr;
        GameObjectManager* objects = nullptr;
        GameObject* selected = nullptr;

        /// ビュー行列 × 射影行列（Draw が入れる）
        Matrix4x4 viewProjection{};
    };

    /// @brief シーンビューへ重ねて描き、マウスを受け取る道具（スクリプトのウィンドウとギズモ）の登録先
    class SceneViewTools
    {
    public:
        static SceneViewTools& Get();

        /// @param draw シーンビューを描くフレームごとに呼ばれる
        ScopedRegistration Register(std::function<void(const SceneViewContext&)> draw);

        /// @brief 登録された道具を描く（シーンビューの選択の処理の後、選んだ物のギズモの前に呼ぶ）
        void Draw(const SceneViewContext& context);

        /// @brief 描いている間の情報（Draw の外では nullptr）
        const SceneViewContext* Current() const { return current_; }

        /// @brief 次のフレームのシーンビューの左クリックで、オブジェクトを選ばないようにする
        void RequestMouse() { requested_ = true; }

        /// @brief 前のフレームの道具がマウスを使ったか
        bool WantsMouse() const { return wantsMouse_; }

    private:
        struct Entry
        {
            std::uint64_t id = 0;
            std::function<void(const SceneViewContext&)> draw;
        };

        std::vector<Entry> entries_;
        std::uint64_t lastId_ = 0;
        const SceneViewContext* current_ = nullptr;
        bool requested_ = false;
        bool wantsMouse_ = false;
    };

    /// @brief シーンビューへ重ねて描く（SceneViewTools::Draw の間だけ描ける。奥の物に隠れない）
    namespace SceneOverlay
    {
        /// @brief ワールドの点のシーンビュー上の位置（ImGui の画面座標）
        /// @return カメラの後ろなら false
        bool WorldToScreen(const Vector3& world, ImVec2& screen);

        void Line(const Vector3& from, const Vector3& to, ImU32 color, float thickness);
        void Polyline(const Vector3* points, std::size_t count, bool closed, ImU32 color, float thickness);

        /// @brief 軸に沿った箱の辺
        void WireBox(const Vector3& center, const Vector3& size, ImU32 color, float thickness);

        /// @brief 円の線（normal に垂直な面）
        void WireDisc(const Vector3& center, const Vector3& normal, float radius, ImU32 color, float thickness);

        /// @brief 球の線（軸ごとの円と、カメラから見た輪郭）
        void WireSphere(const Vector3& center, float radius, ImU32 color, float thickness);

        /// @brief 塗った円（カメラの後ろにかかるときは描かない）
        void SolidDisc(const Vector3& center, const Vector3& normal, float radius, ImU32 color);

        /// @brief 塗った四角形（4 点は周りの順。カメラの後ろにかかるときは描かない）
        void SolidQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, ImU32 color);

        /// @brief 画面の上で size ピクセルの点
        void Dot(const Vector3& position, float size, ImU32 color);

        /// @brief 点の右下に文字（影つき）
        void Label(const Vector3& position, const std::string& text, ImU32 color);
    }
}

#endif // CORE_EDITOR

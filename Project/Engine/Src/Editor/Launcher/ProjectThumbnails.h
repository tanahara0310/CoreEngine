#pragma once

#ifdef CORE_EDITOR

#include <d3d12.h>
#include <imgui.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace CoreEngine
{
    class GraphicsCore;
}

namespace CoreEngine::Editor
{
    /// @brief プロジェクトのサムネイル（`Application/Saved/Thumbnail.png`）
    /// @details 撮るのはエディタを閉じるとき。一覧に出すときは PNG を読み込んで ImGui で描ける形にして持つ。
    class ProjectThumbnails
    {
    public:
        /// @brief folder のプロジェクトのサムネイルの置き場
        static std::filesystem::path FilePath(const std::filesystem::path& projectFolder);

        /// @brief 描画先の絵を縮めて、サムネイルとして保存する
        /// @param source 撮る描画先（リニアの色）
        /// @param state source の今の状態（撮ったあとも同じ状態に戻す）
        /// @param aspect 絵の縦横比（幅 / 高さ）
        /// @param file 保存先
        /// @return 保存できたら true
        static bool Capture(GraphicsCore& graphics, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                            float aspect, const std::filesystem::path& file);

        /// @param graphics 読み込んだ絵を置く GPU（ImGui と同じディスクリプタヒープを使うもの）
        explicit ProjectThumbnails(GraphicsCore& graphics);

        /// @brief folder のプロジェクトのサムネイル（無ければ 0）
        /// @details 初めて聞かれたときに読み込み、以後は同じものを返す。
        ImTextureID Get(const std::filesystem::path& projectFolder);

    private:
        /// @brief 読み込んだサムネイル 1 つ
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            ImTextureID id = 0;
        };

        GraphicsCore& graphics_;
        std::unordered_map<std::wstring, Entry> entries_;
    };
}

#endif // CORE_EDITOR

#include "pch.h"
#include "TextureImportInspector.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Import/TextureImporter.h"
#include "Utility/Logger/Logger.h"

#include <array>
#include <string>

namespace CoreEngine::Editor::TextureImportInspector
{
    namespace
    {
        /// @brief 選べる値 1 つ（.meta に書く値と、欄に出す名前）
        struct Choice
        {
            const char* value;
            const char* label;
        };

        constexpr const char* kDefaultFormatLabel =
            (TextureImporter::kDefaultCompression == TextureCompression::Auto) ? "既定（法線マップは BC5・透明があれば BC3・無ければ BC1）" :
            (TextureImporter::kDefaultCompression == TextureCompression::BC3) ? "既定（BC3）" :
            (TextureImporter::kDefaultCompression == TextureCompression::BC1) ? "既定（BC1）" :
            (TextureImporter::kDefaultCompression == TextureCompression::None) ? "既定（圧縮しない）" : "既定（BC7）";

        constexpr std::array kColorSpaces = {
            Choice{ "", "使い道に合わせる" },
            Choice{ "srgb", "sRGB（色）" },
            Choice{ "linear", "リニア（データ）" },
            Choice{ "normal", "法線マップ（データ）" },
        };

        constexpr std::array kFormats = {
            Choice{ "", kDefaultFormatLabel },
            Choice{ "bc7", "BC7" },
            Choice{ "bc3", "BC3" },
            Choice{ "bc1", "BC1（透明なし）" },
            Choice{ "bc5", "BC5（赤と緑だけ）" },
            Choice{ "none", "圧縮しない" },
        };

        /// 9 スライスの縁の欄の成分の文字
        constexpr const char* kBorderLabels[4] = { "左", "上", "右", "下" };

        /// 9 スライスの縁の上限（px）
        constexpr float kMaxBorder = 16384.0f;

        /// @brief 左にラベル・右に欄の 1 行を始める
        void BeginRow(const char* label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
        }

        /// @brief 値を選ぶ欄
        /// @return 選び直したら true
        template <size_t N>
        bool DrawChoice(const char* id, const std::array<Choice, N>& choices, std::string& value)
        {
            const Choice* current = &choices[0];
            for (const Choice& choice : choices) {
                if (value == choice.value) {
                    current = &choice;
                }
            }
            bool changed = false;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(id, current->label)) {
                for (const Choice& choice : choices) {
                    if (ImGui::Selectable(choice.label, &choice == current) && &choice != current) {
                        value = choice.value;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    bool DrawFile(const std::filesystem::path& file)
    {
        if (!CookedTexture::IsCookable(file)) {
            return false;
        }
        AssetDatabase& database = AssetDatabase::GetInstance();
        const std::string path = Logger::GetInstance().PathToUtf8(file);
        UI::Separator();
        UI::SectionHeader("取り込み設定");
        if (!database.FindAssetByPath(path)) {
            InspectorLayout::Hint("登録されていない画像です（.meta がありません）。");
            return true;
        }

        // 縁は動かしている間は欄の中だけで持ち、離したときに .meta へ書く
        static std::string editingBorderPath;
        static std::array<float, 4> editingBorder{};

        std::string colorSpace = database.GetImportColorSpace(path);
        std::string format = database.GetImportFormat(path);
        std::array<float, 4> border = editingBorderPath == path ? editingBorder : database.GetSliceBorder(path);
        bool changed = false;
        if (ImGui::BeginTable("##textureImport", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            BeginRow("色空間");
            changed |= DrawChoice("##colorSpace", kColorSpaces, colorSpace);
            BeginRow("圧縮");
            changed |= DrawChoice("##format", kFormats, format);
            BeginRow("9 スライスの縁");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (InspectorLayout::VectorField("##sliceBorder", border.data(), 4, 1.0f, 0.0f, kMaxBorder,
                    kBorderLabels, "%.0f")) {
                editingBorderPath = path;
                editingBorder = border;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("UI 画像の種類を「9 スライス」にしたとき、伸ばさずに描く縁の幅（画像の px）");
            }
            ImGui::EndTable();
        }
        if (changed) {
            database.SetImportSettings(file, colorSpace, format);
        }
        if (editingBorderPath == path && !ImGui::IsAnyItemActive()) {
            database.SetSliceBorder(file, editingBorder);
            editingBorderPath.clear();
        }
        InspectorLayout::Hint("変えた設定は .meta に書き、次に読み込むときの変換に使います。"
            "開いているシーンの画像は、エディタを開き直すと変わります（9 スライスの縁はすぐに効きます）。");
        return true;
    }
}

#endif // CORE_EDITOR

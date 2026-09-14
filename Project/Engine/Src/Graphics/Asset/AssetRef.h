#pragma once

#include "Graphics/Asset/AssetType.h"
#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <string_view>

namespace CoreEngine
{
    struct AssetInfo;

    /// @brief `AssetRef` がテクスチャを指すことを表す型
    struct TextureAsset { static constexpr AssetType kType = AssetType::Texture; };

    /// @brief `AssetRef` がモデルを指すことを表す型
    struct ModelAsset { static constexpr AssetType kType = AssetType::Model; };

    /// @brief `AssetRef` が音声を指すことを表す型
    struct AudioAsset { static constexpr AssetType kType = AssetType::Audio; };

    /// @brief `AssetRef` がプレハブを指すことを表す型
    struct PrefabAsset { static constexpr AssetType kType = AssetType::Prefab; };

    /// @brief `AssetRef` が JSON のデータを指すことを表す型
    struct JsonAsset { static constexpr AssetType kType = AssetType::Json; };

    /// @brief `AssetRef` が CSV のデータを指すことを表す型
    struct CsvAsset { static constexpr AssetType kType = AssetType::Csv; };

    /// @brief アセットの、プロジェクトの根からの相対パス（区切りは `/`）
    std::string ToAssetPath(const AssetInfo& info);

    /// @brief パスかファイル名でアセットを引く
    /// @param pathOrName `Application/Assets/…` のような相対パス、絶対パス、またはファイル名（UTF-8）
    /// @param type ファイル名で引くときに探す種類（`AssetType::Unknown` なら種類を問わない）
    /// @return 見つからなければ nullptr
    const AssetInfo* FindAssetInfo(std::string_view pathOrName, AssetType type = AssetType::Unknown);

    /// @brief 参照の値からアセットを引く（GUID で引けなければパスで引く）
    /// @return 見つからなければ nullptr
    const AssetInfo* ResolveAssetRef(const Reflection::AssetRefValue& value);

    /// @brief パスの文字列か `{"guid", "path"}` で保存したアセットの指し先から、今のパスを読む
    /// @param node 保存した値（文字列・`{"guid", "path"}`・null）
    /// @param context 警告の先頭に付ける、読み込み元の説明
    /// @return GUID で引ければその今のパス、引けなければ控えたパス。何も指さなければ空
    /// @note 見つからない・GUID が見つからずパスで引いた・パスが GUID の指す先と食い違う、はそれぞれ警告する。
    std::string JsonToAssetPath(const json& node, std::string_view context);

    /// @brief アセットのパスを `{"guid", "path"}` の保存形にする
    /// @return 空なら null。引ければ今の GUID とパス、引けなければパスだけ
    json AssetPathToJson(std::string_view path);

    /// @brief ディスク上のアセットを GUID とパスで指す参照の、型に依らない部分
    class AssetRefBase
    {
    public:
        /// @brief 控えている GUID（無ければ空）
        const std::string& GetGuid() const noexcept { return guid_; }

        /// @brief 指す先のパス
        /// @return 引ければその今のパス、引けなければ控えているパス
        std::string GetPath() const;

        /// @brief 何かを指しているか
        bool IsSet() const noexcept { return !guid_.empty() || !path_.empty(); }

        /// @brief 指せるアセットの種類
        AssetType GetAssetType() const noexcept { return type_; }

        /// @brief 型記述子とやり取りする値にする（控えている GUID とパスそのまま）
        Reflection::AssetRefValue GetValue() const { return Reflection::AssetRefValue{ guid_, path_ }; }

        /// @brief 型記述子から受け取った値を設定する
        void SetValue(const Reflection::AssetRefValue& value);

        /// @brief 何も指さない状態に戻す
        void Reset();

    protected:
        explicit AssetRefBase(AssetType type) noexcept : type_(type) {}

        /// @brief パスかファイル名で指す（引ければ GUID と今のパスを控える）
        void AssignPath(std::string_view pathOrName);

    private:
        AssetType   type_ = AssetType::Unknown;
        std::string guid_;
        std::string path_;
    };

    /// @brief `T` が表す種類のアセットを指す参照（`T` は `AudioAsset` など）
    /// @details 型記述子に `REFLECT_ASSET_REF` で載せると `{"guid": …, "path": …}` で保存され、
    ///          インスペクタで指す先を選べる。
    template <class T>
    class AssetRef final : public AssetRefBase
    {
    public:
        static constexpr AssetType kAssetType = T::kType;

        AssetRef() noexcept : AssetRefBase(kAssetType) {}

        /// @brief パスかファイル名で指す
        explicit AssetRef(std::string_view pathOrName) : AssetRefBase(kAssetType) { AssignPath(pathOrName); }

        /// @brief パスかファイル名で指し直す（空なら何も指さない）
        void SetPath(std::string_view pathOrName) { AssignPath(pathOrName); }
    };
}

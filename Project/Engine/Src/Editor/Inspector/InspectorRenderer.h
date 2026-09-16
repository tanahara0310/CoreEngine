#pragma once

#include "Utility/JsonManager/JsonManager.h"

#include <functional>
#include <string>

namespace CoreEngine::Reflection
{
    struct TypeDescriptor;
    struct PropertyDescriptor;
}

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief 型記述子からインスペクタの UI を組み立てる
    namespace InspectorRenderer
    {
        /// @brief Hierarchy からオブジェクトをドラッグするときのペイロード名（中身は `ObjectId::value`）
        inline constexpr const char* kObjectDragPayload = "GAME_OBJECT_ID";

        /// @brief 描画のついでに必要になる情報
        struct DrawContext
        {
            /// @brief 履歴に出す名前（空なら型の表示名を使う）
            std::string label;

            /// @brief 値を持っている実体（コンポーネントなど）
            /// @note 破棄時に `EditorCommandStack::RemoveCommandsReferencing` へ渡すと、
            ///       解放済みメモリを触る Undo が履歴から消える。
            const void* owner = nullptr;

            /// @brief 値が書き換わった直後の通知（派生データの再計算に使う）
            /// @note 編集時だけでなく Undo / Redo の適用時にも呼ばれる。
            std::function<void(const Reflection::PropertyDescriptor&)> onChanged;

            /// @brief ObjectRef の繋ぎ先の候補を探すシーン
            const GameObjectManager* objects = nullptr;

            /// @brief 元のプレハブでのこのコンポーネントの parameters（プレハブから作っていなければ nullptr）
            /// @note プレハブと違う値のプロパティに印を付け、右クリックでプレハブの値に戻せるようにする。
            const json* prefabParameters = nullptr;

            /// @brief プロパティの今の値をプレハブへ書き戻す（空ならメニューに出さない）
            std::function<void(const Reflection::PropertyDescriptor&)> applyToPrefab;

            /// @brief 新しく作ったときの値（保存形。分からなければ nullptr）
            /// @note 既定値と違うプロパティのラベルを橙にし、右クリックで既定値に戻せるようにする。
            const json* defaultParameters = nullptr;
        };

        /// @brief 記述子のプロパティを、左にラベル・右に欄の行で順に描く
        /// @param type 型記述子
        /// @param instance 記述子が想定する型の先頭アドレス
        /// @param context 履歴名・所有者・変更通知
        /// @return 値が変更されたら true
        /// @note 編集が確定（ドラッグを離した瞬間）したら `EditorCommandStack` へ 1 件積む。
        ///       呼び出し側が Undo のために書くコードは無い。
        bool Draw(const Reflection::TypeDescriptor& type, void* instance,
                  const DrawContext& context = {});

        /// @brief 既定値と違うプロパティをすべて既定値へ戻す
        /// @return 1 つでも書き換えたら true
        /// @note 戻した分をまとめて 1 件だけ `EditorCommandStack` へ積む。
        bool ResetToDefaults(const Reflection::TypeDescriptor& type, void* instance,
                             const DrawContext& context);

        /// @brief 記述子からインスペクタを組み立てるか（CVar のトグル）
        bool IsEnabled();
    }
}

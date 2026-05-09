import bpy

# ブレンダーに登録するアドオン情報
bl_info = {
    "name": "レベルエディタ",
    "author": "Taro Kamata",
    "version": (1, 0),
    "blender": (4, 4, 1),
    "location": "",
    "description": "レベルエディタ",
    "warning": "",
    "wiki_url": "",
    "tracker_url": "",
    "category": "Object"
}

# アドオン有効時コールバック
def register():
    print("レベルエディタが有効化されました。")

#アドオン無効時コールバック
def unregister():
    print("レベルエディタが無効化されました。")
    
if __name__ == "__main__":
    register()
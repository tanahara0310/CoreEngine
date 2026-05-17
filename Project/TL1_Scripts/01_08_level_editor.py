import bpy
import math
import bpy_extras



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

#トップバーの拡張クラス
class TOPBAR_MT_my_menu(bpy.types.Menu):

    # Blenderがクラスを認識するための固有の文字列
    bl_idname = "TOPBAR_MT_my_menu"

    #メニューのラベルとして表示される文字列
    bl_label = "My Menu"

    # 著者表示用の文字列
    bl_description = "拡張メニュー by " + bl_info["author"]

    #サブメニューの描画
    def draw(self, context):

        # トップバーの「エディターメニュー」に項目を追加
        #self.layout.operator("wm.url_open", text="Manual", icon='HELP')
        self.layout.operator(MYADDON_OT_stretch_vertex.bl_idname, 
            text= MYADDON_OT_stretch_vertex.bl_label)
        
        #ICO球生成オペレータをメニューに追加
        self.layout.operator(MYADDON_OT_create_ico_sphere.bl_idname,
            text= MYADDON_OT_create_ico_sphere.bl_label)
        
        # シーン出力オペレータをメニューに追加
        self.layout.operator(MYADDON_OT_export_scene.bl_idname,
            text= MYADDON_OT_export_scene.bl_label)

    #既存のメニューにサブメニューに追加
    def submenu(self, context):

        #ID指定でサブメニューを追加
        self.layout.menu(TOPBAR_MT_my_menu.bl_idname)

        
# オペレータ　頂点を伸ばす
class MYADDON_OT_stretch_vertex(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_stretch_vertex"
    bl_label = "頂点を伸ばす"
    bl_description = "頂点座標を引っ張って伸ばします"
    #Redo/Undoに対応させるためのオプション
    bl_options = {'REGISTER', 'UNDO'}

    #メニューを実行したときに呼び出されるコールバック関数
    def execute(self, context):
        bpy.data.objects['Cube'].data.vertices[0].co.x += 1.0
        print("頂点を伸ばしました")

        #オペレータの命令終了
        return {'FINISHED'}


#オペレータ ICO球生成
class MYADDON_OT_create_ico_sphere(bpy.types.Operator):
    bl_idname = "myaddon.myaddon_ot_create_ico_sphere"
    bl_label = "ICO球を生成"
    bl_description = "ICO球を生成します"
    bl_options = {'REGISTER', 'UNDO'}

# メニューを実行したときに呼び出されるコールバック関数
    def execute(self, context):
        bpy.ops.mesh.primitive_ico_sphere_add()
        print("ICO球を生成しました")
        return {'FINISHED'}

#オペレータ シーン出力
class MYADDON_OT_export_scene(bpy.types.Operator, bpy_extras.io_utils.ExportHelper):
    bl_idname = "myaddon.export_scene"
    bl_label = "シーンを出力"
    bl_description = "シーン情報をexportします"
    # 出力するファイルの拡張子
    filename_ext = ".scene"

    def export(self):
        """ファイルに出力"""

        print("シーン情報を出力開始... %r" % self.filepath)

        # ファイルをテキスト形式で書き出し用にオープン
        # スコープを抜けると自動的にクローズ
        with open(self.filepath, "wt") as file:

            #ファイルに文字列を書き込む
            self.write_and_print(file, "SCENE")

            #シーン内の全オブジェクトについて
            for object in bpy.context.scene.objects:
             
             # 嫌オブジェクトがあるものはスキップ
             if(object.parent):
                continue
             
             # シーン直下のオブジェクトをルートノードとし，再帰関数で走査
             self.parse_scene_recursive(file, object, 0)
             

        return {'FINISHED'}
    
    def parse_scene_recursive(self, file, object, level):
        """シーン解析用再帰関数"""

        #深さ分インデントする(タブを挿入)
        indent = ''
        for i in range(level):
            indent += "\t"

        #オブジェクト名書込み
        self.write_and_print(file, indent + object.type)
        trans, rot, scale = object.matrix_local.decompose()
        #回転をQuternionからオイラー角に変換
        rot = rot.to_euler()
        #ラジアンから度に変換
        rot.x = math.degrees(rot.x)
        rot.y = math.degrees(rot.y)
        rot.z = math.degrees(rot.z)
        #トランスフォーム情報を表示
        self.write_and_print(file, indent + "T Trans(%f,%f,%f)" % (trans.x, trans.y, trans.z))
        self.write_and_print(file, indent + "R Rot(%f,%f,%f)" % (rot.x, rot.y, rot.z))
        self.write_and_print(file, indent + "S Scale(%f,%f,%f)" % (scale.x, scale.y, scale.z))

        #カスタムプロパティ 'file_name'
        if 'file_name' in object:
            self.write_and_print(file, indent + "N %s " % object['file_name'])
        self.write_and_print(file, indent + 'END')
        self.write_and_print(file, '')

        # 子ノードへ進む
        for child in object.children:
            self.parse_scene_recursive(file, child, level + 1)
        
    
    # ファイルに文字列を書き込むと同時にコンソールにも出力する関数
    def write_and_print(self, file, str):
        print(str)

        file.write(str)
        file.write("\n")

    # メニューを実行したときに呼び出されるコールバック関数
    def execute(self, context):
        print("シーン情報をexportします")

        # ファイルに出力
        self.export()

    #シーン内の全オブジェクトについて
        for object in bpy.context.scene.objects:
         print(object.type + " - " + object.name)

         #ローカルトランスフォーム行列から平行移動，回転，スケーリングを取得
         trans, rot, scale = object.matrix_local.decompose()
         #回転をクォータニオンからオイラー角に変換
         rot = rot.to_euler()
         #ラジアンから度に変換
         rot.x = math.degrees(rot.x)
         rot.y = math.degrees(rot.y)
         rot.z = math.degrees(rot.z)
         # トランスフォーム情報を表示
         print("Trans(%f,%f,%f)" % (trans.x, trans.y, trans.z))
         print("Rot(%f,%f,%f)" % (rot.x, rot.y, rot.z))
         print("Scale(%f,%f,%f)" % (scale.x, scale.y, scale.z))
         #親オブジェクトの名前を表示
         if object.parent:
            print("Parent: " + object.parent.name)
         print()


        return {'FINISHED'}
    
# パネル ファイル名
class OBJECT_PT_file_name(bpy.types.Panel):
     """オブジェクトのファイルネームパネル"""
     bl_idname = "OBJECT_PT_file_name"
     bl_label = "File Name"
     bl_space_type = "PROPERTIES"
     bl_region_type = "WINDOW"
     bl_context = "object"

     #サブメニューの描画
     def draw(self, context):
         
         #パネルに項目を追加
         if 'file_name' in context.object:
             self.layout.prop(context.object, '["file_name"]', text = self.bl_label)
         else:
             #プロパティが無ければ，プロパティ追加ボタンを表示
                self.layout.operator(MYADDON_OT_add_filename.bl_idname)

#オペレータ　カスタムプロパティ['file_name']追加
class MYADDON_OT_add_filename(bpy.types.Operator):
     bl_idname = "myaddon.myaddon_ot_add_filename"
     bl_label = "FileNameを追加"
     bl_description = "['file_name']カスタムプロパティを追加します"
     bl_options = {'REGISTER', 'UNDO'}

     def execute(self, context):
         
         #['file_name']カスタムプロパティを追加
         context.object['file_name'] = ""
         return {'FINISHED'}

#Blenderに登録するクラスリスト
classes = (
    MYADDON_OT_stretch_vertex,
    MYADDON_OT_create_ico_sphere,
    MYADDON_OT_export_scene,
    TOPBAR_MT_my_menu,
    OBJECT_PT_file_name,
    MYADDON_OT_add_filename,
)


# アドオン有効時コールバック
def register():

    #Blenderにクラス登録
    for cls in classes:
        bpy.utils.register_class(cls)

    #メニューに項目を追加
    bpy.types.TOPBAR_MT_editor_menus.append(TOPBAR_MT_my_menu.submenu)

    print("レベルエディタが有効化されました。")


#アドオン無効時コールバック
def unregister():

    #メニューから項目を削除
    bpy.types.TOPBAR_MT_editor_menus.remove(TOPBAR_MT_my_menu.submenu)

    #Blenderからクラス登録解除
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)

    print("レベルエディタが無効化されました。")


if __name__ == "__main__":
    register()
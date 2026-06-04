import bpy
import math
import bpy_extras
import gpu
import gpu_extras.batch
import copy
import mathutils
import json



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
    filename_ext = ".json"

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
    
    def export_json(self):
        """JSON形式でファイルに出力"""

        # 保存する情報をまとめるdict
        json_object_root = dict()

        # ノード名
        json_object_root["name"] = "scene"
        #オブジェクトリストを作成
        json_object_root["objects"] = list()

        # Todo : シーン内の全オブジェクト走査してパック
        #シーン内の全オブジェクトについて
        for object in bpy.context.scene.objects:
             
             # 親オブジェクトがあるものはスキップ
             if(object.parent):
                continue
             
             # シーン直下のオブジェクトをルートノードとし，再帰関数で走査
             self.parse_scene_recursive_json(json_object_root["objects"], object, 0)

        #オブジェクトをjson文字列にエンコード
        json_text = json.dumps(json_object_root, ensure_ascii=False, cls=json.JSONEncoder, indent=4)
        #コンソールに表示してみる
        print(json_text)

        #ファイルをテキスト形式で書き出し用にオープン
        #スコープを抜けると自動的にクローズ
        with open(self.filepath, "wt", encoding="utf-8") as file:
            #ファイルにjson文字列を書き込む
            file.write(json_text)


    def parse_scene_recursive_json(self, data_parent, object, level):
            """シーン解析用再帰関数"""

            #オブジェクトの情報をまとめるdict
            json_object = dict()
            #オブジェクト種類
            json_object["type"] = object.type
            # オブジェクト名
            json_object["name"] = object.name

            #Todo " その他情報をパック"
            #ローカルトランスフォーム行列から平行移動，回転，スケーリングを取得
            trans, rot, scale = object.matrix_local.decompose()
            #回転をクォータニオンからオイラー角に変換
            rot = rot.to_euler()
            #ラジアンから度に変換
            rot.x = math.degrees(rot.x)
            rot.y = math.degrees(rot.y)
            rot.z = math.degrees(rot.z)
            # トランスフォーム情報をまとめて登録
            transform = dict()
            transform["translation"] = [trans.x, trans.y, trans.z]
            transform["rotation"] = [rot.x, rot.y, rot.z]
            transform["scaling"] = [scale.x, scale.y, scale.z]
            json_object["transform"] = transform

            #カスタムプロパティ 'file_name'
            if 'file_name' in object:
                json_object["file_name"] = object['file_name']

            # カスタムプロパティ 'collision'
            if 'collider' in object:
                collider = dict()
                collider["type"] = object['collider']
                collider["center"] = object["collider_center"].to_list()
                collider["size"] = object["collider_size"].to_list()
                json_object["collider"] = collider

            # 一個分のjsonオブジェクトを親オブジェクトに登録
            data_parent.append(json_object)

            # Todo : 直接の子供リストを走査
            # 子ノードがあれば
            if len(object.children) > 0:
                #子ノード用のリストを作成して親オブジェクトに登録
                json_object["children"] = list()
                #子ノードを走査
                for child in object.children:
                    self.parse_scene_recursive_json(json_object["children"], child, level + 1)

    
           
    
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

        # カスタムプロパティ 'collision'
        if 'collider' in object:
            self.write_and_print(file, indent + "C %s" % object['collider'])
            temp_str = indent + "CC %f %f %f"
            temp_str %= (object['collider_center'][0], object['collider_center'][1], object['collider_center'][2])
            self.write_and_print(file, temp_str)
            temp_str = indent + "CS %f %f %f"
            temp_str %= (object['collider_size'][0], object['collider_size'][1], object['collider_size'][2])
            self.write_and_print(file, temp_str)
            
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
        self.export_json()

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
     

# コライダー描画クラス
class DrawCollider:

    # 描画ハンドル
    handle = None

    #3dビューにコライダーを描画する関数
    def draw_collider():

        # 頂点データ
        vertices = {"pos": []}
        #インデックスデータ
        indices = []

        # 各頂点のオブジェクトの中心からのオフセット
        offsets = [ 
                    [-0.5, -0.5, -0.5], # 左下前
                    [+0.5, -0.5, -0.5], # 右下前
                    [-0.5, +0.5, -0.5], # 左上前
                    [+0.5, +0.5, -0.5], # 右上前
                    [-0.5, -0.5, +0.5], # 左下奥
                    [+0.5, -0.5, +0.5], # 右下奥
                    [-0.5, +0.5, +0.5], # 左上奥
                    [+0.5, +0.5, +0.5], # 右上奥
        ]

        # 立方体のx,y,zサイズ
        size = [2,2,2]

        # 現在のシーンの全オブジェクトを走査
        for object in bpy.context.scene.objects:

                # コライダープロパティがあるオブジェクトだけ処理
            if not 'collider' in object:
                continue

            #中心点，サイズの変数を宣言
            center = mathutils.Vector((0.0, 0.0, 0.0))
            size = mathutils.Vector((2.0, 2.0, 2.0))

            #コライダープロパティから中心点とサイズを取得
            center[2] = object['collider_center'][0]
            center[1] = object['collider_center'][1]
            center[0] = object['collider_center'][2]
            size[0] = object['collider_size'][0]
            size[1] = object['collider_size'][1]
            size[2] = object['collider_size'][2]

            #追加前の頂点数
            start = len(vertices["pos"])

            #Boxの8頂点回す
            for offset in offsets:
                #オブジェクトの中心座標をコピー
                pos = copy.copy(center)

                #中心点を基準に各頂点ごとずらす
                pos[0] += offset[0] * size[0]
                pos[1] += offset[1] * size[1]
                pos[2] += offset[2] * size[2]
                #ローカル座標からワールド座標に変換
                pos = object.matrix_world @ pos
                #頂点データリストに座標を追加
                vertices["pos"].append(pos)

                # 前面を構成する辺の頂点インデックス
                indices.append([start + 0, start + 1]) # 左下前 - 右下前
                indices.append([start + 2, start + 3]) # 左上前 - 右上前
                indices.append([start + 0, start + 2]) # 左下前 - 左上前
                indices.append([start + 1, start + 3]) # 右下前 - 右上前
                # 背面を構成する辺の頂点インデックス
                indices.append([start + 4, start + 5]) # 左下奥 - 右下奥
                indices.append([start + 6, start + 7]) # 左上奥 - 右上奥
                indices.append([start + 4, start + 6]) # 左下奥 - 左上奥
                indices.append([start + 5, start + 7]) # 右下奥 - 右上奥
                # 前面と背面をつなぐ辺の頂点インデックス
                indices.append([start + 0, start + 4]) # 左下前 - 左下奥
                indices.append([start + 1, start + 5]) # 右下前 - 右下奥
                indices.append([start + 2, start + 6]) # 左上前 - 左上奥
                indices.append([start + 3, start + 7]) # 右上前 - 右上奥



        # ビルドインのシェーダーを取得
        shader = gpu.shader.from_builtin('UNIFORM_COLOR')

        # バッチを作成
        batch = gpu_extras.batch.batch_for_shader(shader, 'LINES', vertices, indices = indices)

        # シェーダーのパラメータ設定
        color = (0.5, 1.0, 1.0, 1.0)
        shader.bind()
        shader.uniform_float("color", color)
        #描画
        batch.draw(shader)


# コライダー追加オペレータ
class MYADDON_OT_add_collider(bpy.types.Operator):

    # ['collider']カスタムプロパティを追加するオペレータ
    bl_idname = "myaddon.myaddon_ot_add_collider"
    bl_label = "コライダーを追加"
    bl_description = "['collider']カスタムプロパティを追加します"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):

        # ['collider']カスタムプロパティを追加
        context.object['collider'] = "BOX"
        context.object['collider_center'] = mathutils.Vector((0.0, 0.0, 0.0))
        context.object['collider_size'] = mathutils.Vector((2.0, 2.0, 2.0))

        return {'FINISHED'}
    
#パネルコライダー
class OBJECT_PT_collider(bpy.types.Panel):
        """オブジェクトのコライダーパネル"""
        bl_idname = "OBJECT_PT_collider"
        bl_label = "Collider"
        bl_space_type = "PROPERTIES"
        bl_region_type = "WINDOW"
        bl_context = "object"
    
        #サブメニューの描画
        def draw(self, context):

            # パネルに項目を追加
            if 'collider' in context.object:
                #既にプロパティがあれば，プロパティを編集するUIを表示
                self.layout.prop(context.object, '["collider"]', text = "Collider Type")
                self.layout.prop(context.object, '["collider_center"]', text = "Collider Center")
                self.layout.prop(context.object, '["collider_size"]', text = "Collider Size")
            else:
                # プロパティが無ければ，プロパティ追加ボタンを表示
                self.layout.operator(MYADDON_OT_add_collider.bl_idname)

#Blenderに登録するクラスリスト
classes = (
    MYADDON_OT_stretch_vertex,
    MYADDON_OT_create_ico_sphere,
    MYADDON_OT_export_scene,
    TOPBAR_MT_my_menu,
    OBJECT_PT_file_name,
    MYADDON_OT_add_filename,
    MYADDON_OT_add_collider,
    OBJECT_PT_collider,
)


# アドオン有効時コールバック
def register():

    #Blenderにクラス登録
    for cls in classes:
        bpy.utils.register_class(cls)

    #メニューに項目を追加
    bpy.types.TOPBAR_MT_editor_menus.append(TOPBAR_MT_my_menu.submenu)

    # 3dビューにコライダー描画関数を登録
    DrawCollider.handle = bpy.types.SpaceView3D.draw_handler_add(DrawCollider.draw_collider, (), 'WINDOW', 'POST_VIEW')

    print("レベルエディタが有効化されました。")


#アドオン無効時コールバック
def unregister():

    #メニューから項目を削除
    bpy.types.TOPBAR_MT_editor_menus.remove(TOPBAR_MT_my_menu.submenu)

    # 3dビューからコライダー描画関数を削除
    bpy.types.SpaceView3D.draw_handler_remove(DrawCollider.handle, 'WINDOW')

    #Blenderからクラス登録解除
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)

    print("レベルエディタが無効化されました。")


if __name__ == "__main__":
    register()
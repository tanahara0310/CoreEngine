"""Mixamo の X Bot（FBX）を glTF にし、待機・歩き・走りのアニメーションを付ける（Blender で流す）

    blender -b --factory-startup --python Tools/XBot/build_xbot.py -- "<X Bot.fbx>"

出力:
  - Projects/Sandbox/Application/Assets/Models/XBot/XBot.gltf（と XBot.bin）
      骨（mixamorig:* の 65 本）と皮膚の重みは FBX のまま。アニメーションは Idle（6 秒）・Walk（0.9 秒）・Run（0.7 秒）の 3 本で、
      どれもその場で足踏みする形（腰は前へ進まない）。Walk は 1.4m/s、Run は 3.5m/s で進んだときに足が滑らない歩幅にしてある
  - Tools/XBot/XBot.blend
      書き出す前の Blender のシーン（3 本のアクションを NLA に並べてある）
骨の cm 単位と Z 上の回転は骨へ焼き込み、骨の根は単位行列にする。正面は glTF の +Z（Blender の -Y）。
姿勢は 30fps の毎コマを計算して打つ。脚は足首の通り道から 2 本の骨の IK で解き、腰は踏んでいる足に届く高さまで下げる。
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not ARGS:
    raise SystemExit('使い方: blender -b --factory-startup --python Tools/XBot/build_xbot.py -- "<X Bot.fbx>"')
SRC = ARGS[0]
OUT_GLTF = os.path.join(REPO, "Projects", "Sandbox", "Application", "Assets", "Models", "XBot", "XBot.gltf")
OUT_BLEND = os.path.join(HERE, "XBot.blend")
FPS = 30


def P(name):
    return "mixamorig:" + name


# ---- 読み込み ----

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS
bpy.ops.import_scene.fbx(filepath=SRC)

arm = next(o for o in scene.objects if o.type == 'ARMATURE')
meshes = [o for o in scene.objects if o.type == 'MESH']

if arm.animation_data:
    arm.animation_data_clear()
for action in list(bpy.data.actions):
    bpy.data.actions.remove(action)
for pb in arm.pose.bones:
    pb.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()

# 骨の根の cm 単位と回転を骨と頂点へ焼き込む
bpy.ops.object.select_all(action='DESELECT')
for o in [arm] + meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.context.view_layer.update()

# 体と関節の 2 つのメッシュを 1 つにまとめる（材質は 2 つのまま）
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
mesh_obj = next(o for o in meshes if 'Surface' in o.name)
bpy.context.view_layer.objects.active = mesh_obj
bpy.ops.object.join()
mesh_obj.name = "XBot_Mesh"
mesh_obj.data.name = "XBot_Mesh"
arm.name = "XBot"
arm.data.name = "XBot_Skeleton"
for material in mesh_obj.data.materials:
    material.name = "XBot_Joints" if 'Joints' in material.name else "XBot_Body"

# ---- 骨の休み姿勢 ----

BONES = arm.data.bones
REST_ROT = {b.name: b.matrix_local.to_quaternion() for b in BONES}
HEAD = {b.name: b.head_local.copy() for b in BONES}

FWD = Vector((0, -1, 0))    # キャラの正面（Blender の -Y）
UP = Vector((0, 0, 1))
LEFT = Vector((1, 0, 0))    # キャラの左


def rot(axis, deg):
    return Quaternion(axis, math.radians(deg))


def lean(deg):
    """正で上の端が前へ倒れる"""
    return rot(LEFT, deg)


def bank(deg):
    """正で左側が下がる"""
    return rot(FWD, -deg)


def turn(deg):
    """正で正面が左を向く"""
    return rot(UP, deg)


def perp(v, axis):
    a = axis.normalized()
    return (v - a * v.dot(a)).normalized()


def frame(d, f):
    d = d.normalized()
    f = perp(f, d)
    return Matrix((d, f, d.cross(f))).transposed()


def align(d_rest, f_rest, d_des, f_des):
    """骨の向き d と前 f を、休み姿勢から目的の向きへ回す回転"""
    return (frame(d_des, f_des) @ frame(d_rest, f_rest).transposed()).to_quaternion()


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def keyed(keys, u):
    """(u, 値) の並びを smoothstep でつなぐ"""
    for i in range(len(keys) - 1):
        u0, v0 = keys[i]
        u1, v1 = keys[i + 1]
        if u <= u1:
            return v0 + (v1 - v0) * smooth((u - u0) / (u1 - u0))
    return keys[-1][1]


def hermite(p0, p1, m0, m1, u):
    u2, u3 = u * u, u * u * u
    return ((2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 +
            (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1)


# 脚の長さと、足のかかと・つま先の付け根（地面に着く点）
SIDES = {"L": ("Left", 1.0), "R": ("Right", -1.0)}
LEGS = {}
verts = [mesh_obj.matrix_world @ v.co for v in mesh_obj.data.vertices]
for key, (name, sx) in SIDES.items():
    up_leg, leg, foot, toe = P(name + "UpLeg"), P(name + "Leg"), P(name + "Foot"), P(name + "ToeBase")
    ankle = HEAD[foot]
    side = LEFT * sx
    sole = [w for w in verts if w.z < 0.03 and abs((w - ankle).dot(side)) < 0.07]
    heel = ankle + (-FWD) * max((w - ankle).dot(-FWD) for w in sole)
    heel.z = 0.0
    ball = HEAD[toe].copy()
    ball.z = 0.0
    LEGS[key] = dict(up=up_leg, leg=leg, foot=foot, toe=toe, sx=sx,
                     l1=(HEAD[leg] - HEAD[up_leg]).length, l2=(HEAD[foot] - HEAD[leg]).length,
                     ankle=ankle.copy(), heel=heel, ball=ball,
                     thigh_d=HEAD[leg] - HEAD[up_leg], shin_d=HEAD[foot] - HEAD[leg])

HIPS = P("Hips")


def foot_place(leg, fwd, lift, pitch, lat, yaw):
    """足を 1 枚の板として、かかと（pitch > 0：つま先が上）かつま先の付け根（pitch < 0：かかとが上）で転がす"""
    ankle_rest = leg["ankle"]
    offset = FWD * fwd + LEFT * (lat * leg["sx"] - ankle_rest.dot(LEFT)) + UP * lift
    pivot = leg["heel"] if pitch > 0 else leg["ball"]
    r = rot(LEFT, -pitch)
    ankle = pivot + offset + r @ (ankle_rest - pivot)
    return ankle, turn(yaw * leg["sx"]) @ r


# ---- 動きの定義 ----

def leg_cycle(c, phi):
    """1 周期の中の位置 phi（0〜1）での、足の前後・持ち上げ・傾き・つま先・着いているか"""
    duty = c["duty"]
    stride = c["v"] * c["T"] * duty
    front = c["center"] + stride / 2
    back = c["center"] - stride / 2
    if phi < duty:
        u = phi / duty
        pitch = keyed(c["stance_pitch"], u)
        return front - stride * u, 0.0, pitch, (pitch if pitch > 0 else 0.0), True
    u = (phi - duty) / (1 - duty)
    swing_time = (1 - duty) * c["T"]
    fwd = hermite(back, front, -c["v"] * swing_time * c["m0k"], -c["v"] * swing_time * c["m1k"], u)
    lift = c["lift"] * math.sin(math.pi * (u ** c["lift_pow"]))
    pitch = keyed(c["swing_pitch"], u)
    return fwd, max(0.0, lift), pitch, pitch * smooth(u / 0.35), False


def arms_pose(Q, c, swing_l, swing_r, elbow_l, elbow_r, curl, clav):
    """腕を体の横へ下ろし、前後に振る（swing > 0 で前・elbow > 0 で肘が前へ曲がる）"""
    for key, (name, sx) in SIDES.items():
        swing = swing_l if key == "L" else swing_r
        elbow = elbow_l if key == "L" else elbow_r
        Q[P(name + "Shoulder")] = bank(clav * sx) @ turn(-c.get("clav_fwd", 0.0) * swing / 30.0 * sx)
        Q[P(name + "Arm")] = lean(-swing) @ bank((c["arm_down"] - clav) * sx) @ rot(LEFT, c.get("arm_in", 0.0))
        Q[P(name + "ForeArm")] = turn(-elbow * sx)
        Q[P(name + "Hand")] = bank(c.get("wrist", 0.0) * sx)
        for finger, spread in (("Index", 1.0), ("Middle", 1.05), ("Ring", 1.1), ("Pinky", 1.15)):
            for j, k in ((1, 0.8), (2, 1.0), (3, 0.8)):
                Q[P(name + "Hand" + finger + str(j))] = bank(curl * k * spread * sx)
        for j, k in ((1, 0.25), (2, 0.35), (3, 0.35)):
            Q[P(name + "HandThumb" + str(j))] = bank(curl * k * sx) @ turn(curl * 0.3 * k * sx)


def spine_pose(Q, yaw, roll, lean_deg, breath=0.0, head_yaw=0.0, head_pitch=0.0, counter=1.7):
    """背骨で腰のひねりと傾きを打ち消し、首と頭で正面を向かせる"""
    for bone, share in ((P("Spine"), 0.3), (P("Spine1"), 0.3), (P("Spine2"), 0.4)):
        chest = breath * share if bone != P("Spine") else 0.0
        Q[bone] = turn(-yaw * counter * share) @ bank(-roll * share) @ lean(lean_deg * share - chest)
    chest_world = Q[HIPS] @ Q[P("Spine")] @ Q[P("Spine1")] @ Q[P("Spine2")]
    correction = chest_world.inverted() @ (turn(head_yaw) @ lean(head_pitch))
    neck = Quaternion().slerp(correction, 0.4)
    Q[P("Neck")] = neck
    Q[P("Head")] = neck.inverted() @ correction


def gait_clip(c):
    """歩き・走りの 1 周期（t = 0〜1。左足が着く瞬間が 0）"""
    def pose(t):
        Q = {}
        legs = {}
        for key in ("L", "R"):
            phi = (t + (0.0 if key == "L" else 0.5)) % 1.0
            fwd, lift, pitch, toe, stance = leg_cycle(c, phi)
            legs[key] = dict(fwd=fwd, lift=lift, pitch=pitch, toe=toe, stance=stance, lat=c["lat"], yaw=c["toe_out"])
        two = 2 * math.pi
        hips = (HEAD[HIPS] + LEFT * (c["sway"] * math.cos(two * (t - c["sway_phase"])))
                + UP * (-c["base_drop"] - c["bob"] * math.cos(2 * two * (t - c["bob_phase"]))))
        yaw = c["yaw"] * math.cos(two * t)
        roll = c["roll"] * math.cos(two * (t - c["roll_phase"]))
        Q[HIPS] = turn(-yaw) @ bank(-roll) @ lean(c["tilt"])
        spine_pose(Q, -yaw, -roll, c["lean"], head_pitch=c.get("head_pitch", 0.0), counter=c["counter"])
        sw_l = c["arm_mean"] + c["arm_swing"] * math.cos(two * (t - 0.5 - c["arm_lag"]))
        sw_r = c["arm_mean"] + c["arm_swing"] * math.cos(two * (t - c["arm_lag"]))
        el_l = c["elbow"] + c["elbow_swing"] * (sw_l - c["arm_mean"]) / max(c["arm_swing"], 1e-3)
        el_r = c["elbow"] + c["elbow_swing"] * (sw_r - c["arm_mean"]) / max(c["arm_swing"], 1e-3)
        arms_pose(Q, c, sw_l, sw_r, el_l, el_r, c["curl"], c["clav"])
        return dict(Q=Q, hips=hips, legs=legs)
    return pose


# T: 1 周期の秒数 / v: 進む速さ m/s / duty: 足が着いている割合 / center: 着いている間の足の中ほどの前後位置
# stance_pitch・swing_pitch: 着いている間・浮いている間の足の傾き（度。正でつま先が上）
# lift: 浮いている間の持ち上げ / bob・base_drop: 腰の上下と下げ幅 / yaw・roll・tilt: 腰のひねり・傾き・前傾
# lean: 背骨の前傾 / arm_*: 腕の下げ角・振り・肘 / curl: 指の曲げ / Lk: 踏んでいる脚を伸ばしきらない割合
WALK = dict(T=27.0 / 30.0, v=1.4, duty=0.6, lat=0.085, center=-0.03,
            stance_pitch=[(0, 14), (0.18, 0), (0.55, 0), (1.0, -30)],
            swing_pitch=[(0, -30), (0.3, -18), (0.65, 0), (1.0, 14)],
            m0k=0.8, m1k=0.25, lift=0.05, lift_pow=0.75,
            bob=0.016, base_drop=0.022, bob_phase=0.05,
            sway=0.018, sway_phase=0.3, yaw=5.0, roll=3.0, roll_phase=0.15, tilt=2.0, lean=3.0, counter=1.8,
            arm_down=74.0, clav=6.0, clav_fwd=3.0, arm_swing=17.0, arm_mean=-2.0, arm_lag=0.03,
            elbow=16.0, elbow_swing=8.0, curl=22.0, toe_out=5.0, Lk=0.99, wrist=6.0)

RUN = dict(T=0.7, v=3.5, duty=0.33, lat=0.07, center=-0.05,
           stance_pitch=[(0, 6), (0.2, 0), (0.45, 0), (1.0, -36)],
           swing_pitch=[(0, -36), (0.3, -48), (0.75, -4), (1.0, 6)],
           m0k=0.9, m1k=0.35, lift=0.30, lift_pow=0.6,
           bob=0.028, base_drop=0.052, bob_phase=0.165,
           sway=0.008, sway_phase=0.17, yaw=7.0, roll=2.5, roll_phase=0.1, tilt=5.0, lean=9.0, counter=1.9,
           arm_down=70.0, clav=4.0, clav_fwd=4.0, arm_swing=36.0, arm_mean=6.0, arm_lag=0.04, arm_in=6.0,
           elbow=82.0, elbow_swing=14.0, curl=55.0, toe_out=2.0, Lk=0.97, head_pitch=4.0, wrist=0.0)

IDLE_T = 6.0


def idle_clip():
    """立ったまま、息で胸を上下させ、重心を左右へゆっくり移す（6 秒で 2 呼吸）"""
    def pose(t):
        two = 2 * math.pi
        breath = math.sin(two * 2 * t)
        sway = math.sin(two * t)
        Q = {}
        hips = HEAD[HIPS] + LEFT * (0.016 * sway) + UP * (-0.014 - 0.002 * breath)
        roll = -1.8 * sway
        yaw = 1.5 * math.sin(two * t + 0.7)
        Q[HIPS] = turn(yaw) @ bank(roll) @ lean(1.0)
        spine_pose(Q, yaw * 0.3, roll * 0.6, 1.5, breath=0.9 * breath,
                   head_yaw=3.0 * math.sin(two * t + 1.3), head_pitch=2.0 + 0.6 * breath, counter=0.6)
        arms = dict(arm_down=75.0, clav_fwd=0.0, wrist=8.0)
        sw = 1.5 * math.sin(two * t)
        arms_pose(Q, arms, 2.0 + sw, 2.0 - sw, 14.0 + 1.5 * breath, 14.0 + 1.5 * breath, 24.0, 6.0 - 1.2 * breath)
        legs = {
            "L": dict(fwd=0.03, lift=0.0, pitch=0.0, toe=0.0, stance=True, lat=0.10, yaw=8.0),
            "R": dict(fwd=-0.02, lift=0.0, pitch=0.0, toe=0.0, stance=True, lat=0.10, yaw=8.0),
        }
        return dict(Q=Q, hips=hips, legs=legs)
    return pose


# ---- 姿勢を解く ----

def solve_legs(pose):
    """足首の位置へ 2 本の骨の IK で脚を合わせ、足とつま先の向きを付ける（膝は腰の正面へ向ける）"""
    Q = pose["Q"]
    hips_q = Q[HIPS]
    for key, lp in pose["legs"].items():
        leg = LEGS[key]
        target, foot_q = foot_place(leg, lp["fwd"], lp["lift"], lp["pitch"], lp["lat"], lp["yaw"])
        hip = pose["hips"] + hips_q @ (HEAD[leg["up"]] - HEAD[HIPS])
        l1, l2 = leg["l1"], leg["l2"]
        v = target - hip
        u = v / v.length
        reach = max(abs(l1 - l2) + 1e-4, min(v.length, (l1 + l2) * 0.9999))
        along = (l1 * l1 - l2 * l2 + reach * reach) / (2 * reach)
        out = math.sqrt(max(l1 * l1 - along * along, 0.0))
        knee_dir = perp(hips_q @ (FWD + LEFT * leg["sx"] * 0.12), u)
        knee = hip + u * along + knee_dir * out
        ankle = hip + u * reach
        thigh = align(leg["thigh_d"], FWD, knee - hip, knee_dir)
        shin = align(leg["shin_d"], FWD, ankle - knee, knee_dir)
        toe_q = turn(lp["yaw"] * leg["sx"]) @ rot(LEFT, -lp["toe"])
        Q[leg["up"]] = hips_q.inverted() @ thigh
        Q[leg["leg"]] = thigh.inverted() @ shin
        Q[leg["foot"]] = shin.inverted() @ foot_q
        Q[leg["toe"]] = foot_q.inverted() @ toe_q


def hip_limit(pose, Lk):
    """踏んでいる足に、脚を伸ばしきらずに届く腰の高さの上限"""
    hips_q = pose["Q"][HIPS]
    limit = 1e9
    for key, lp in pose["legs"].items():
        if not lp["stance"]:
            continue
        leg = LEGS[key]
        target, _ = foot_place(leg, lp["fwd"], lp["lift"], lp["pitch"], lp["lat"], lp["yaw"])
        offset = hips_q @ (HEAD[leg["up"]] - HEAD[HIPS])
        hip = pose["hips"] + offset
        horizontal = math.hypot(target.x - hip.x, target.y - hip.y)
        length = (leg["l1"] + leg["l2"]) * Lk
        if horizontal < length:
            limit = min(limit, target.z + math.sqrt(length * length - horizontal * horizontal) - offset.z)
    return limit


def circular_smooth(values, radius):
    n = len(values)
    return [sum(values[(i + k) % n] for k in range(-radius, radius + 1)) / (2 * radius + 1) for i in range(n)]


def basis_of(name, Q):
    """休み姿勢の向きで表した回転 Q を、ポーズの骨のローカルの回転にする"""
    r = REST_ROT[name]
    return r.inverted() @ Q.get(name, Quaternion()) @ r


def build(name, clip, frames, Lk):
    """毎コマの姿勢を計算してアクションに打ち、NLA に並べる（最後のコマは最初と同じ）"""
    poses = [clip(f / frames) for f in range(frames)]
    limits = [hip_limit(p, Lk) for p in poses]
    heights = [min(p["hips"].z, lim) for p, lim in zip(poses, limits)]
    for _ in range(2):
        heights = [min(h, lim) for h, lim in zip(circular_smooth(heights, 1), limits)]
    for p, h in zip(poses, heights):
        p["hips"].z = h
        solve_legs(p)

    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    arm.animation_data_create()
    arm.animation_data.action = action
    previous = {}
    for f in range(frames + 1):
        p = poses[f % frames]
        for pb in arm.pose.bones:
            q = basis_of(pb.name, p["Q"])
            if pb.name in previous and previous[pb.name].dot(q) < 0:
                q = -q
            previous[pb.name] = q
            pb.rotation_quaternion = q
            pb.keyframe_insert("rotation_quaternion", frame=f, group=pb.name)
            if pb.name == HIPS:
                pb.location = REST_ROT[HIPS].inverted() @ (p["hips"] - HEAD[HIPS])
                pb.keyframe_insert("location", frame=f, group=pb.name)
    track = arm.animation_data.nla_tracks.new()
    track.name = name
    strip = track.strips.new(name, 0, action)
    strip.action_frame_start = 0
    strip.action_frame_end = frames
    track.mute = True
    arm.animation_data.action = None
    print(f"{name}: {frames} コマ、腰の下げ幅 {HEAD[HIPS].z - max(heights):.3f}〜{HEAD[HIPS].z - min(heights):.3f}m")


bpy.context.preferences.edit.keyframe_new_interpolation_type = 'LINEAR'
for pb in arm.pose.bones:
    pb.rotation_mode = 'QUATERNION'

build("Idle", idle_clip(), int(round(IDLE_T * FPS)), 0.99)
build("Walk", gait_clip(WALK), int(round(WALK["T"] * FPS)), WALK["Lk"])
build("Run", gait_clip(RUN), int(round(RUN["T"] * FPS)), RUN["Lk"])

for pb in arm.pose.bones:
    pb.matrix_basis = Matrix.Identity(4)
scene.frame_start = 0
scene.frame_end = int(round(IDLE_T * FPS))
scene.frame_set(0)

bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)

os.makedirs(os.path.dirname(OUT_GLTF), exist_ok=True)
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.export_scene.gltf(
    filepath=OUT_GLTF,
    export_format='GLTF_SEPARATE',
    use_selection=False,
    export_yup=True,
    export_apply=False,
    export_texcoords=True,
    export_normals=True,
    export_materials='EXPORT',
    export_cameras=False,
    export_lights=False,
    export_skins=True,
    export_morph=False,
    export_animations=True,
    export_animation_mode='ACTIONS',
    export_force_sampling=True,
    export_frame_step=1,
    export_reset_pose_bones=True,
    export_anim_single_armature=True,
    export_optimize_animation_size=False,
)
print("書き出し:", OUT_GLTF, OUT_BLEND)

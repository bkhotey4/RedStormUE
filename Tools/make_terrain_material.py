# 紅色風暴：在 Unreal 編輯器內（指令列模式）匯入地表貼圖並建立地形材質 M_RSTerrain
# 由「地表材質_建立.bat」呼叫：UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<本檔>
import os
import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(ROOT, "SourceArt", "Terrain")
DST = "/Game/RedStorm/Terrain"
TEX = DST + "/Tex"
LOG = os.path.join(ROOT, "Saved", "terrain_material_log.txt")
lines = []

def log(s):
    unreal.log("[RSTerrainMat] " + s)
    lines.append(s)

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# ---- 1. 匯入貼圖 ----
files = sorted(f for f in os.listdir(SRC) if f.startswith("T_") and f.endswith(".png"))
tasks = []
for f in files:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", TEX)
    t.set_editor_property("destination_name", f[:-4])
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    tasks.append(t)
tools.import_asset_tasks(tasks)
log("imported %d textures" % len(files))

for f in files:
    name = f[:-4]
    tex = eal.load_asset(TEX + "/" + name)
    if tex is None:
        log("MISSING " + name)
        continue
    if name.endswith("_N"):
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
    else:
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    eal.save_asset(TEX + "/" + name, only_if_is_dirty=False)

# ---- 2. 建立材質 ----
MAT = DST + "/M_RSTerrain"
if eal.does_asset_exist(MAT):
    eal.delete_asset(MAT)
mat = tools.create_asset("M_RSTerrain", DST, unreal.Material, unreal.MaterialFactoryNew())

def node(cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e

def link(a, aout, b, bin_):
    ok = mel.connect_material_expressions(a, aout, b, bin_)
    if not ok:
        log("link failed %s.%s -> %s.%s" % (a.get_name(), aout, b.get_name(), bin_))

def mul(a, aout, b, bout, x, y):
    m = node(unreal.MaterialExpressionMultiply, x, y)
    link(a, aout, m, "A"); link(b, bout, m, "B")
    return m

def add(a, b, x, y):
    m = node(unreal.MaterialExpressionAdd, x, y)
    link(a, "", m, "A"); link(b, "", m, "B")
    return m

uv0 = node(unreal.MaterialExpressionTextureCoordinate, -2200, 0, coordinate_index=0)
uv1 = node(unreal.MaterialExpressionTextureCoordinate, -2200, 600, coordinate_index=1)
vc = node(unreal.MaterialExpressionVertexColor, -2200, 300)

# 第 5 層（道路）權重 = saturate(1 - R - G - B - A)
s1 = node(unreal.MaterialExpressionAdd, -2000, 300); link(vc, "R", s1, "A"); link(vc, "G", s1, "B")
s2 = node(unreal.MaterialExpressionAdd, -1900, 300); link(s1, "", s2, "A"); link(vc, "B", s2, "B")
s3 = node(unreal.MaterialExpressionAdd, -1800, 300); link(s2, "", s3, "A"); link(vc, "A", s3, "B")
om = node(unreal.MaterialExpressionOneMinus, -1700, 300); link(s3, "", om, "")
w4 = node(unreal.MaterialExpressionSaturate, -1600, 300); link(om, "", w4, "")
weights = [(vc, "R"), (vc, "G"), (vc, "B"), (vc, "A"), (w4, "")]

defaults = ["Grass", "Dirt", "Sand", "Rock", "Asphalt"]
acc_a = None
acc_n = None
for i in range(5):
    y = -900 + i * 420
    ta = node(unreal.MaterialExpressionTextureSampleParameter2D, -1400, y,
              parameter_name="L%d" % i, texture=eal.load_asset(TEX + "/T_%s_A" % defaults[i]))
    link(uv0, "", ta, "UVs")
    tn = node(unreal.MaterialExpressionTextureSampleParameter2D, -1400, y + 200,
              parameter_name="N%d" % i, texture=eal.load_asset(TEX + "/T_%s_N" % defaults[i]),
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    link(uv0, "", tn, "UVs")
    ma = mul(ta, "RGB", weights[i][0], weights[i][1], -1100, y)
    mn = mul(tn, "RGB", weights[i][0], weights[i][1], -1100, y + 200)
    acc_a = ma if acc_a is None else add(acc_a, ma, -900, y)
    acc_n = mn if acc_n is None else add(acc_n, mn, -900, y + 200)

# 大尺度明暗變化（打破重複感）：Macro 貼圖以 0.13 倍 UV 取樣
muv = node(unreal.MaterialExpressionMultiply, -1700, 900, const_b=0.13); link(uv0, "", muv, "A")
mac = node(unreal.MaterialExpressionTextureSampleParameter2D, -1400, 900,
           parameter_name="Macro", texture=eal.load_asset(TEX + "/T_Macro_A"))
link(muv, "", mac, "UVs")
mlerp = node(unreal.MaterialExpressionLinearInterpolate, -1100, 900, const_a=0.8, const_b=1.18)
link(mac, "R", mlerp, "Alpha")
# 頂點明暗（UV1.x，由 C++ 依雜訊給 0.8~1.15）
tone = node(unreal.MaterialExpressionComponentMask, -1900, 600, r=True, g=False, b=False, a=False)
link(uv1, "", tone, "")
b1 = node(unreal.MaterialExpressionMultiply, -700, 0); link(acc_a, "", b1, "A"); link(mlerp, "", b1, "B")
b2 = node(unreal.MaterialExpressionMultiply, -500, 0); link(b1, "", b2, "A"); link(tone, "", b2, "B")
tint = node(unreal.MaterialExpressionVectorParameter, -700, 200, parameter_name="Tint",
            default_value=unreal.LinearColor(1, 1, 1, 1))
b3 = node(unreal.MaterialExpressionMultiply, -300, 0); link(b2, "", b3, "A"); link(tint, "", b3, "B")
mel.connect_material_property(b3, "", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(acc_n, "", unreal.MaterialProperty.MP_NORMAL)
rough = node(unreal.MaterialExpressionScalarParameter, -500, 400, parameter_name="Roughness", default_value=0.9)
mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
spec = node(unreal.MaterialExpressionConstant, -500, 500, r=0.35)
mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

mel.recompile_material(mat)
eal.save_asset(MAT, only_if_is_dirty=False)
log("material saved %s, expressions=%d" % (MAT, mel.get_num_material_expressions(mat)))

# ---- 3. 水面材質 M_RSWater：深淺漸層、雙層流動波紋、岸邊浪花、緩慢起伏 ----
WMAT = DST + "/M_RSWater"
if eal.does_asset_exist(WMAT):
    eal.delete_asset(WMAT)
mat = tools.create_asset("M_RSWater", DST, unreal.Material, unreal.MaterialFactoryNew())

def custom(x, y, code, inputs, otype):
    c = node(unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", otype)
    arr = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        arr.append(ci)
    c.set_editor_property("inputs", arr)
    return c

wuv = node(unreal.MaterialExpressionTextureCoordinate, -2000, 0, coordinate_index=0)
wvc = node(unreal.MaterialExpressionVertexColor, -2000, 400)
wt = node(unreal.MaterialExpressionTime, -2000, 700)
uvB = node(unreal.MaterialExpressionMultiply, -1800, 200, const_b=1.7); link(wuv, "", uvB, "A")
pA = node(unreal.MaterialExpressionPanner, -1600, 0, speed_x=0.022, speed_y=0.012); link(wuv, "", pA, "Coordinate")
pB = node(unreal.MaterialExpressionPanner, -1600, 200, speed_x=-0.016, speed_y=0.024); link(uvB, "", pB, "Coordinate")
uvF = node(unreal.MaterialExpressionMultiply, -1800, 500, const_b=0.8); link(wuv, "", uvF, "A")
pF = node(unreal.MaterialExpressionPanner, -1600, 500, speed_x=0.03, speed_y=-0.02); link(uvF, "", pF, "Coordinate")
nA = node(unreal.MaterialExpressionTextureSampleParameter2D, -1300, 0, parameter_name="WaveN",
          texture=eal.load_asset(TEX + "/T_Water_N"), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
link(pA, "", nA, "UVs")
nB = node(unreal.MaterialExpressionTextureSampleParameter2D, -1300, 200, parameter_name="WaveN2",
          texture=eal.load_asset(TEX + "/T_Water_N"), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
link(pB, "", nB, "UVs")
fT = node(unreal.MaterialExpressionTextureSampleParameter2D, -1300, 500, parameter_name="Foam",
          texture=eal.load_asset(TEX + "/T_Foam_A"))
link(pF, "", fT, "UVs")
shallow = node(unreal.MaterialExpressionVectorParameter, -1300, 800, parameter_name="Shallow", default_value=unreal.LinearColor(0.08, 0.32, 0.34, 1))
deep = node(unreal.MaterialExpressionVectorParameter, -1300, 950, parameter_name="Deep", default_value=unreal.LinearColor(0.012, 0.06, 0.12, 1))
strength = node(unreal.MaterialExpressionScalarParameter, -1300, 1100, parameter_name="WaveStrength", default_value=0.55)

col = custom(-900, 300,
    "float d = saturate(Depth);\n"
    "float3 c = lerp(Shallow, Deep, d * d * (3 - 2 * d));\n"
    "float f = saturate(FoamTex * Foam * 2.4 - 0.45);\n"
    "return lerp(c, float3(0.85, 0.9, 0.92), f);",
    ["Depth", "Foam", "FoamTex", "Shallow", "Deep"], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(wvc, "R", col, "Depth"); link(wvc, "G", col, "Foam"); link(fT, "R", col, "FoamTex")
link(shallow, "", col, "Shallow"); link(deep, "", col, "Deep")
mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

nrm = custom(-900, 0,
    "float3 n = normalize(float3(A.xy + B.xy, A.z * B.z));\n"
    "return normalize(lerp(float3(0, 0, 1), n, S));",
    ["A", "B", "S"], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(nA, "RGB", nrm, "A"); link(nB, "RGB", nrm, "B"); link(strength, "", nrm, "S")
mel.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)

rgh = custom(-900, 600, "return lerp(0.04, 0.55, saturate(FoamTex * Foam * 2.4 - 0.45));",
    ["Foam", "FoamTex"], unreal.CustomMaterialOutputType.CMOT_FLOAT1)
link(wvc, "G", rgh, "Foam"); link(fT, "R", rgh, "FoamTex")
mel.connect_material_property(rgh, "", unreal.MaterialProperty.MP_ROUGHNESS)
wspec = node(unreal.MaterialExpressionConstant, -900, 750, r=0.7)
mel.connect_material_property(wspec, "", unreal.MaterialProperty.MP_SPECULAR)

wp = node(unreal.MaterialExpressionWorldPosition, -1300, 1250)
wpo = custom(-900, 1000,
    "float h = sin(P.x * 0.012 + T * 1.1) * 0.6 + sin(P.y * 0.017 - T * 0.9) * 0.4 + sin((P.x + P.y) * 0.031 + T * 2.0) * 0.2;\n"
    "return float3(0, 0, h * 2.5 * saturate(Depth * 2.0));",
    ["P", "T", "Depth"], unreal.CustomMaterialOutputType.CMOT_FLOAT3)
link(wp, "", wpo, "P"); link(wt, "", wpo, "T"); link(wvc, "R", wpo, "Depth")
_wpo_names = [n for n in dir(unreal.MaterialProperty) if "WORLD" in n and "OFFSET" in n]
log("wpo enum candidates: %s | all: %s" % (_wpo_names, [n for n in dir(unreal.MaterialProperty) if n.startswith("MP_")]))
if _wpo_names:
    mel.connect_material_property(wpo, "", getattr(unreal.MaterialProperty, _wpo_names[0]))

mel.recompile_material(mat)
eal.save_asset(WMAT, only_if_is_dirty=False)
log("water material saved, expressions=%d" % mel.get_num_material_expressions(mat))
log("TERRAIN_MATERIAL_OK")
with open(LOG, "w", encoding="utf-8") as fp:
    fp.write("\n".join(lines))

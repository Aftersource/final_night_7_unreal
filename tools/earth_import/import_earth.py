"""Headless: /Game/Earth/SM_HexPrism (from hex_prism.obj) and /Game/Earth/M_EarthColumn (per-instance custom data:
0-2 colour A, 3 wet A, 4 heat A, 5-7 colour B, 8 wet B, 9 heat B; scalar BeatT lerps A -> B on the GPU)."""
import os
import unreal

here = os.path.dirname(os.path.abspath(__file__))
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary

task = unreal.AssetImportTask()
task.filename = os.path.join(here, "hex_prism.obj")
task.destination_path = "/Game/Earth"
task.destination_name = "SM_HexPrism"
task.automated = True
task.replace_existing = True
task.save = True
tools.import_asset_tasks([task])

path = "/Game/Earth/M_EarthColumn"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
mat = tools.create_asset("M_EarthColumn", "/Game/Earth", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("used_with_instanced_static_meshes", True)

def node(cls, x, y):
    return lib.create_material_expression(mat, cls, x, y)

pic = []
for k in range(10):
    n = node(unreal.MaterialExpressionPerInstanceCustomData, -1400, k * 90)
    n.set_editor_property("data_index", k)
    pic.append(n)
t = node(unreal.MaterialExpressionScalarParameter, -1400, 1000)
t.set_editor_property("parameter_name", "BeatT")
t.set_editor_property("default_value", 0.0)

def append3(a, b, c, x, y):
    ab = node(unreal.MaterialExpressionAppendVector, x, y)
    lib.connect_material_expressions(a, "", ab, "A")
    lib.connect_material_expressions(b, "", ab, "B")
    abc = node(unreal.MaterialExpressionAppendVector, x + 160, y)
    lib.connect_material_expressions(ab, "", abc, "A")
    lib.connect_material_expressions(c, "", abc, "B")
    return abc

def lerp(a, b, x, y):
    n = node(unreal.MaterialExpressionLinearInterpolate, x, y)
    lib.connect_material_expressions(a, "", n, "A")
    lib.connect_material_expressions(b, "", n, "B")
    lib.connect_material_expressions(t, "", n, "Alpha")
    return n

col_a = append3(pic[0], pic[1], pic[2], -1100, 0)
col_b = append3(pic[5], pic[6], pic[7], -1100, 300)
col = lerp(col_a, col_b, -700, 100)
wet = lerp(pic[3], pic[8], -700, 400)
heat = lerp(pic[4], pic[9], -700, 600)

# base colour darkens when wet; roughness drops when wet; lava/fire glow by heat
dark = node(unreal.MaterialExpressionMultiply, -450, 400)
lib.connect_material_expressions(wet, "", dark, "A")
dark.set_editor_property("const_b", 0.45)
one_minus = node(unreal.MaterialExpressionOneMinus, -300, 400)
lib.connect_material_expressions(dark, "", one_minus, "")
base = node(unreal.MaterialExpressionMultiply, -150, 100)
lib.connect_material_expressions(col, "", base, "A")
lib.connect_material_expressions(one_minus, "", base, "B")
lib.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

rough = node(unreal.MaterialExpressionMultiply, -300, 520)
lib.connect_material_expressions(wet, "", rough, "A")
rough.set_editor_property("const_b", -0.6)
rough_add = node(unreal.MaterialExpressionAdd, -150, 520)
lib.connect_material_expressions(rough, "", rough_add, "A")
rough_add.set_editor_property("const_b", 0.85)
lib.connect_material_property(rough_add, "", unreal.MaterialProperty.MP_ROUGHNESS)

glow = node(unreal.MaterialExpressionConstant3Vector, -450, 700)
glow.set_editor_property("constant", unreal.LinearColor(8.0, 2.4, 0.3, 1.0))
emis = node(unreal.MaterialExpressionMultiply, -150, 700)
lib.connect_material_expressions(glow, "", emis, "A")
lib.connect_material_expressions(heat, "", emis, "B")
lib.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log_warning("FN7EARTH ok mesh=%s mat=%s" % (unreal.EditorAssetLibrary.does_asset_exist("/Game/Earth/SM_HexPrism"), unreal.EditorAssetLibrary.does_asset_exist(path)))

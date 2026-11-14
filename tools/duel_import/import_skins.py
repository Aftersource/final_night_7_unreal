"""Duel mannequin skins (step A, Mac): the 5 skins' PBR maps, one master material, five instances.

Run headless in a content-only scratch project (PythonScriptPlugin + EditorScriptingUtilities), then copy
Content/Duel/Skins into the game project:
  FN7_SKINS_DIR=<repo>/production_assets/meshy/mannequin/skins \
  UnrealEditor Scratch.uproject -run=pythonscript -script=<repo>/tools/duel_import/import_skins.py -unattended -nosplash -nullrhi -stdout

Result in /Game/Duel/Skins:
  <skin>/T_<skin>_{base_color,normal,metallic,roughness}   base color sRGB; normal TC_Normalmap with the green channel
                                                            flipped (glTF/Meshy normals are OpenGL +Y, Unreal is -Y);
                                                            metallic / roughness linear masks
  M_DuelSkin                                                parameters BaseColor, Normal, Metallic, Roughness; used with
                                                            skeletal meshes
  MI_Skin_<skin>                                            one instance per skin (a skin is a material swap: shared UVs)
"""
import os
import unreal

SRC = os.environ.get("FN7_SKINS_DIR", "")
SKINS = ["wood", "obsidian", "gold", "dragonscale", "butterfly"]  # the FNV-1a order in docs/DUEL_UNREAL.md
MAPS = ["base_color", "normal", "metallic", "roughness"]
ROOT = "/Game/Duel/Skins"

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def tex_path(skin, m):
    return "%s/%s/T_%s_%s" % (ROOT, skin, skin, m)


# 1. the textures
tasks = []
for skin in SKINS:
    for m in MAPS:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(SRC, skin, "texture_0_%s.png" % m)
        t.destination_path = "%s/%s" % (ROOT, skin)
        t.destination_name = "T_%s_%s" % (skin, m)
        t.automated = True
        t.save = True
        t.replace_existing = True
        tasks.append((skin, m, t))
tools.import_asset_tasks([t for _, _, t in tasks])
for skin, m, t in tasks:
    want = tex_path(skin, m)
    got = [p for p in t.imported_object_paths if lib.does_asset_exist(p.split(".")[0])]
    if not lib.does_asset_exist(want) and got:
        lib.rename_asset(got[0].split(".")[0], want)  # the importer kept the file name
    tex = unreal.load_asset(want)
    if tex is None:
        unreal.log_error("FN7SKINS missing %s (from %s)" % (want, t.filename))
        continue
    if m == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)
    elif m in ("metallic", "roughness"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    lib.save_loaded_asset(tex)
unreal.log("FN7SKINS textures done")

# 2. the master material
mat_path = ROOT + "/M_DuelSkin"
if lib.does_asset_exist(mat_path):
    lib.delete_asset(mat_path)
mat = tools.create_asset("M_DuelSkin", ROOT, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("used_with_skeletal_mesh", True)
SAMPLER = {
    "base_color": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
    "normal": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
    "metallic": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
    "roughness": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
}
PARAM = {"base_color": "BaseColor", "normal": "Normal", "metallic": "Metallic", "roughness": "Roughness"}
nodes = {}
for i, m in enumerate(MAPS):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -300 + 260 * i)
    n.set_editor_property("parameter_name", PARAM[m])
    n.set_editor_property("texture", unreal.load_asset(tex_path("wood", m)))
    n.set_editor_property("sampler_type", SAMPLER[m])
    nodes[m] = n
mel.connect_material_property(nodes["base_color"], "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(nodes["normal"], "RGB", unreal.MaterialProperty.MP_NORMAL)
mel.connect_material_property(nodes["metallic"], "R", unreal.MaterialProperty.MP_METALLIC)
mel.connect_material_property(nodes["roughness"], "R", unreal.MaterialProperty.MP_ROUGHNESS)
mel.recompile_material(mat)
lib.save_loaded_asset(mat)
unreal.log("FN7SKINS material done")

# 3. one instance per skin
for skin in SKINS:
    mi_path = "%s/MI_Skin_%s" % (ROOT, skin)
    if lib.does_asset_exist(mi_path):
        lib.delete_asset(mi_path)
    mi = tools.create_asset("MI_Skin_%s" % skin, ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, mat)
    for m in MAPS:
        mel.set_material_instance_texture_parameter_value(mi, PARAM[m], unreal.load_asset(tex_path(skin, m)))
    lib.save_loaded_asset(mi)

reg = unreal.AssetRegistryHelpers.get_asset_registry()
counts = {}
for a in reg.get_assets_by_path(ROOT, recursive=True):
    c = str(a.asset_class_path.asset_name)
    counts[c] = counts.get(c, 0) + 1
unreal.log("FN7SKINS classes %s" % counts)
lib.save_directory(ROOT, only_if_is_dirty=False, recursive=True)

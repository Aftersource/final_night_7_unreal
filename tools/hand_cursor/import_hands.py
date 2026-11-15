"""Headless: /Game/UI/Hand/T_Hand_{Open,Grab,Point,Fling} (UI textures, no mips) from cut_hands.py's PNGs."""
import os
import unreal

here = os.path.dirname(os.path.abspath(__file__))
tools = unreal.AssetToolsHelpers.get_asset_tools()
n_ok = 0
for n in ["Open", "Grab", "Point", "Fling"]:
    t = unreal.AssetImportTask()
    t.filename = os.path.join(here, "T_Hand_%s.png" % n)
    t.destination_path = "/Game/UI/Hand"
    t.destination_name = "T_Hand_" + n
    t.automated = True
    t.replace_existing = True
    t.save = False
    tools.import_asset_tasks([t])
    tex = unreal.load_asset("/Game/UI/Hand/T_Hand_" + n)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    n_ok += 1
unreal.log_warning("FN7HAND ok %d" % n_ok)

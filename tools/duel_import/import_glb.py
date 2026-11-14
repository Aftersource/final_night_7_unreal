import unreal
SRC = r"C:\Users\User\Documents\FN7DuelImport\src\mannequin_all_clips_notex.glb"
task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = "/Game/Duel/Mannequin"
task.automated = True
task.save = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
paths = list(task.imported_object_paths)
unreal.log("FN7IMPORT imported %d objects" % len(paths))
reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/Duel", recursive=True)
counts = {}
for a in assets:
    c = str(a.asset_class_path.asset_name)
    counts[c] = counts.get(c, 0) + 1
unreal.log("FN7IMPORT classes %s" % counts)
anims = sorted(str(a.asset_name) for a in assets if str(a.asset_class_path.asset_name) == "AnimSequence")
unreal.log("FN7IMPORT anims %d: %s" % (len(anims), ", ".join(anims)))
unreal.EditorAssetLibrary.save_directory("/Game/Duel", only_if_is_dirty=False, recursive=True)

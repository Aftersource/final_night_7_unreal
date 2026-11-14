import unreal
EAL = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
PFX = "mannequin_all_clips_notex"
base = "/Game/Duel/Mannequin"
names = {"SkeletalMesh": "SK_DuelMannequin", "Skeleton": "SKEL_DuelMannequin", "PhysicsAsset": "PHYS_DuelMannequin", "MaterialInstanceConstant": "MI_DuelMannequin_Default"}
for a in reg.get_assets_by_path("/Game/Duel", recursive=True):
    cls = str(a.asset_class_path.asset_name); name = str(a.asset_name); path = str(a.package_name)
    folder = path.rsplit("/", 1)[0]
    if cls == "AnimSequence":
        new = "A_" + (name[len(PFX):] if name.startswith(PFX) else name)
        dest = base + "/Anims/" + new
    elif cls in names:
        dest = base + "/" + names[cls]
    else:
        continue
    if path != dest:
        ok = EAL.rename_asset(path, dest)
        unreal.log("FN7RENAME %s -> %s %s" % (path, dest, "ok" if ok else "FAILED"))
EAL.save_directory("/Game/Duel", only_if_is_dirty=False, recursive=True)
mesh = EAL.load_asset(base + "/SK_DuelMannequin")
if mesh:
    b = mesh.get_bounds()
    unreal.log("FN7CHECK bounds extent cm %s origin %s" % (b.box_extent, b.origin))
    skel = mesh.skeleton
    unreal.log("FN7CHECK skeleton %s" % (skel.get_path_name() if skel else None))
    try:
        refs = unreal.SkeletalMeshEditorSubsystem if hasattr(unreal, "SkeletalMeshEditorSubsystem") else None
    except Exception:
        refs = None
anims = [str(a.asset_name) for a in reg.get_assets_by_path(base + "/Anims", recursive=True)]
unreal.log("FN7CHECK anims %d %s" % (len(anims), " ".join(sorted(anims))))
for n in ("A_idle_guard", "A_strike_slash", "A_run"):
    s = EAL.load_asset(base + "/Anims/" + n)
    if s:
        unreal.log("FN7CHECK %s length %.2fs skeleton %s" % (n, s.get_editor_property("sequence_length") if hasattr(s, "get_editor_property") else -1, s.get_editor_property("skeleton").get_name()))

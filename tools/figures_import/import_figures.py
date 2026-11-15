"""Import Meshy statue GLBs into /Game/Figures/<slug>/ (headless, in a content-only scratch project).

Env: FN7_FIGURES_DIR = production_assets/meshy/figures, FN7_FIGURES = comma-separated slugs (e.g. chris_god,benito_god).
Each statue: Interchange glTF import (static mesh + materials + textures), textures capped at 1024, Nanite on,
renamed to SM_<slug>. Prints one line per statue: triangles, bounds (cm), texture count.
"""
import os
import unreal

src = os.environ["FN7_FIGURES_DIR"]
slugs = [s.strip() for s in os.environ["FN7_FIGURES"].split(",") if s.strip()]
tools = unreal.AssetToolsHelpers.get_asset_tools()
reg = unreal.AssetRegistryHelpers.get_asset_registry()

for entry in slugs:
    # "kestrel_god=bot_kestrel_god": the source folder, filed under the slug the seat code derives from the player name
    srcslug, _, slug = entry.partition("=")
    slug = slug or srcslug
    glb = os.path.join(src, srcslug, srcslug + "_statue.glb")
    if not os.path.exists(glb):
        print("FN7FIG missing", glb)
        continue
    dest = "/Game/Figures/" + slug
    task = unreal.AssetImportTask()
    task.filename = glb
    task.destination_path = dest
    task.automated = True
    task.replace_existing = True
    task.save = False
    tools.import_asset_tasks([task])
    assets = unreal.EditorAssetLibrary.list_assets(dest, recursive=True, include_folder=False)
    mesh = None
    ntex = 0
    for path in assets:
        a = unreal.EditorAssetLibrary.load_asset(path)
        if isinstance(a, unreal.Texture2D):
            a.set_editor_property("max_texture_size", 1024)
            ntex += 1
            unreal.EditorAssetLibrary.save_loaded_asset(a)
        elif isinstance(a, unreal.StaticMesh) and mesh is None:
            mesh = a
    if mesh is None:
        print("FN7FIG no static mesh for", slug, assets)
        continue
    ns = mesh.get_editor_property("nanite_settings")
    ns.enabled = True
    mesh.set_editor_property("nanite_settings", ns)
    want = dest + "/SM_" + slug
    if mesh.get_path_name().split(".")[0] != want:
        unreal.EditorAssetLibrary.rename_asset(mesh.get_path_name().split(".")[0], want)
        mesh = unreal.EditorAssetLibrary.load_asset(want)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    for path in unreal.EditorAssetLibrary.list_assets(dest, recursive=True, include_folder=False):
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=True)
    b = mesh.get_bounds()
    tris = unreal.EditorStaticMeshLibrary.get_number_verts(mesh, 0)
    unreal.log_warning("FN7FIG ok %s mesh=%s verts=%d box=%s tex=%d" % (slug, mesh.get_path_name(), tris, b.box_extent, ntex))

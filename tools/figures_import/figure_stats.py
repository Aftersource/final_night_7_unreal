"""Log each imported statue's size and trim its Nanite data (FN7_FIGURES = slugs, FN7_KEEP = fraction of triangles kept)."""
import os
import unreal

keep = float(os.environ.get("FN7_KEEP", "0.25"))
for slug in [s.strip() for s in os.environ["FN7_FIGURES"].split(",") if s.strip()]:
    path = "/Game/Figures/%s/SM_%s" % (slug, slug)
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if not mesh:
        unreal.log_warning("FN7FIG missing " + path)
        continue
    verts = unreal.EditorStaticMeshLibrary.get_number_verts(mesh, 0)
    ns = mesh.get_editor_property("nanite_settings")
    ns.enabled = True
    ns.keep_percent_triangles = keep
    ns.fallback_percent_triangles = 0.02
    mesh.set_editor_property("nanite_settings", ns)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    b = mesh.get_bounds()
    texs = []
    for p in unreal.EditorAssetLibrary.list_assets("/Game/Figures/" + slug, recursive=True, include_folder=False):
        a = unreal.EditorAssetLibrary.load_asset(p)
        if isinstance(a, unreal.Texture2D):
            texs.append("%dx%d max%d" % (a.blueprint_get_size_x(), a.blueprint_get_size_y(), a.get_editor_property("max_texture_size")))
    unreal.log_warning("FN7FIG %s verts=%d extent=(%.1f, %.1f, %.1f) origin=(%.1f, %.1f, %.1f) keep=%.2f tex=[%s]" % (
        slug, verts, b.box_extent.x, b.box_extent.y, b.box_extent.z, b.origin.x, b.origin.y, b.origin.z, keep, ", ".join(texs)))

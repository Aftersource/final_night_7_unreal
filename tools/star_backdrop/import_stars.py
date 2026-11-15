"""Headless: /Game/Stars/T_Stars_0..3 (the four layers) and /Game/Stars/M_StarSky, one unlit two-sided sky material that
composites the four layers by view direction, each layer turned by its own basis (X0..Z3, set every frame by
AFNStarBackdrop from the globe's rotation at its parallax factor), each with its own brightness (B0..B3)."""
import os
import unreal

here = os.path.dirname(os.path.abspath(__file__))
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.MaterialEditingLibrary
names = ["T_Stars_0_Dust", "T_Stars_1_Far", "T_Stars_2_Mid", "T_Stars_3_Near"]

texs = []
for n in names:
    t = unreal.AssetImportTask()
    t.filename = os.path.join(here, n + ".png")
    t.destination_path = "/Game/Stars"
    t.destination_name = n
    t.automated = True
    t.replace_existing = True
    t.save = False
    tools.import_asset_tasks([t])
    tex = unreal.load_asset("/Game/Stars/" + n)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_BC7)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    texs.append(tex)

path = "/Game/Stars/M_StarSky"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.EditorAssetLibrary.delete_asset(path)
mat = tools.create_asset("M_StarSky", "/Game/Stars", unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("is_sky", True)

def node(cls, x, y):
    return lib.create_material_expression(mat, cls, x, y)

custom = node(unreal.MaterialExpressionCustom, -300, 0)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
inputs = ["V"] + ["T%d" % k for k in range(4)] + ["%s%d" % (a, k) for k in range(4) for a in "XYZ"] + ["B%d" % k for k in range(4)]
ci = []
for nm in inputs:
    c = unreal.CustomInput()
    c.set_editor_property("input_name", nm)
    ci.append(c)
custom.set_editor_property("inputs", ci)
code = """
float3 d = -normalize(V);
float3 acc = 0;
float3 X[4] = { X0.xyz, X1.xyz, X2.xyz, X3.xyz };
float3 Y[4] = { Y0.xyz, Y1.xyz, Y2.xyz, Y3.xyz };
float3 Z[4] = { Z0.xyz, Z1.xyz, Z2.xyz, Z3.xyz };
float  B[4] = { B0, B1, B2, B3 };
[unroll] for (int k = 0; k < 4; ++k)
{
    float3 l = float3(dot(d, X[k]), dot(d, Y[k]), dot(d, Z[k]));
    float2 uv = float2(atan2(l.y, l.x) * 0.15915494 + 0.5, 0.5 - asin(clamp(l.z, -1.0, 1.0)) * 0.31830989);
    float3 c = (k == 0) ? T0.SampleLevel(T0Sampler, uv, 0).rgb : (k == 1) ? T1.SampleLevel(T1Sampler, uv, 0).rgb : (k == 2) ? T2.SampleLevel(T2Sampler, uv, 0).rgb : T3.SampleLevel(T3Sampler, uv, 0).rgb;
    acc += c * B[k];
}
return acc;
"""
custom.set_editor_property("code", code)

cam = node(unreal.MaterialExpressionCameraVectorWS, -900, -300)
lib.connect_material_expressions(cam, "", custom, "V")
for k in range(4):
    t = node(unreal.MaterialExpressionTextureObjectParameter, -900, -150 + k * 120)
    t.set_editor_property("parameter_name", "T%d" % k)
    t.set_editor_property("texture", texs[k])
    t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    lib.connect_material_expressions(t, "", custom, "T%d" % k)
axes = {"X": unreal.LinearColor(1, 0, 0, 0), "Y": unreal.LinearColor(0, 1, 0, 0), "Z": unreal.LinearColor(0, 0, 1, 0)}
for k in range(4):
    for i, a in enumerate("XYZ"):
        v = node(unreal.MaterialExpressionVectorParameter, -1300, -300 + (k * 3 + i) * 70)
        v.set_editor_property("parameter_name", "%s%d" % (a, k))
        v.set_editor_property("default_value", axes[a])
        lib.connect_material_expressions(v, "", custom, "%s%d" % (a, k))
    b = node(unreal.MaterialExpressionScalarParameter, -1300, 600 + k * 70)
    b.set_editor_property("parameter_name", "B%d" % k)
    b.set_editor_property("default_value", 1.0)
    lib.connect_material_expressions(b, "", custom, "B%d" % k)
lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
lib.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log_warning("FN7STARS ok textures=%d mat=%s" % (len(texs), unreal.EditorAssetLibrary.does_asset_exist(path)))

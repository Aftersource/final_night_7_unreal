# Duel mannequin import (step E, Windows): the settings that worked

Done on 2026-11-14 on Benito's PC (UE 5.8, Interchange, headless). The result is in `Content/Duel/Mannequin/`:

| Asset | What it is |
|---|---|
| `SK_DuelMannequin` | the skeletal mesh: 170 cm tall, Z up, feet at Z 0 (bounds extent 88.7 × 13.8 × 85 cm, arms out along X) |
| `SKEL_DuelMannequin` | its Skeleton (UE5 mannequin bone names), shared by every clip |
| `PHYS_DuelMannequin` | the physics asset Interchange made |
| `MI_DuelMannequin_Default` | a plain material instance from the GLB's material (no textures; the skins replace it) |
| `Anims/A_<clip>` × 35 | every clip, named after the server's vocabulary: `A_airborne_hit` … `A_walk`, all on `SKEL_DuelMannequin` (e.g. idle_guard 1.67 s, strike_slash 1.50 s, run 0.63 s) |

## 1. The blocker, and the fix

The GLB as delivered **does not import**. Interchange refuses it:

```
LogInterchangeEngine: Error: Not all required extensions are supported. (Unsupported extensions: EXT_texture_webp)
LogInterchangeEngine: Error: There was nothing to import from the provided source data using the chosen pipeline options.
```

The GLB embeds its 3 textures as WebP and lists `EXT_texture_webp` under `extensionsRequired`. The skins are separate
PNGs and a material swap anyway, so strip the textures:

```
node tools/duel_import/glb_strip_tex.mjs production_assets/meshy/mannequin/mannequin_all_clips.glb mannequin_all_clips_notex.glb
```

That drops images, textures, samplers and every material texture slot, plus the extension. The mesh, skin and the
35 animations are untouched (same BIN chunk); the output is 14,278,924 bytes.

## 2. The import

Run it headless in a content-only scratch project (`{"Plugins":[{"Name":"PythonScriptPlugin","Enabled":true}]}`, no
C++ module, so the stock editor binaries run it while other editors are open):

```
UnrealEditor-Cmd.exe Scratch.uproject -run=pythonscript -script=tools/duel_import/import_glb.py -unattended -nosplash -nullrhi -stdout
```

`import_glb.py` is a plain `unreal.AssetImportTask` (automated, save, replace_existing) to `/Game/Duel/Mannequin`,
with the **Interchange default pipeline, no option changed**. That gives a skeletal mesh, a new skeleton, a physics
asset and the animations. Then `rename_check.py` renames to the names above (the importer prefixes each clip with the
file name) and logs the bounds and the clip list. Then copy `Content/Duel/` into the project.

## 3. Still to do (owners as in DUEL_UNREAL.md)

- The 5 skins (Mac, step A): import `skins/<skin>/texture_0_{base_color,normal,metallic,roughness}.png` (normal map as
  `TC_Normalmap`, metallic/roughness as linear masks). Then one master material with four texture parameters, and five
  `MI_Skin_<skin>` instances.
- Root motion: the clips move the pelvis. Strip its X/Z in the player (as `duel_lab/main.js` does) or turn off root
  motion on the sequences. Nothing was changed at import.
- Facing: arms lie along ±X, so the figure faces ±Y in mesh space. Check against the lab's forward +Z when you place
  it, and rotate the component (not the asset) if needed.
- The FBX (`mannequin_rigged.fbx`) was not needed: the stripped GLB imports cleanly.

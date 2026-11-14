# Meshy for Final Night 7 (research, 2026-11-14)

What Meshy can do for our demigod figures and the duel stage's posable fighters, from Meshy's own docs and changelog
(docs.meshy.ai) plus independent reviews. Claims marked "unverified" could not be confirmed.

## Models (current)
- `latest` = meshy-7.1 (since 18 Sep 2026); meshy-5 retired 10 Oct 2026; meshy-7 stops 31 Dec 2026.
- Geometry: meshy-7.1 (most faithful to the input image, very dense, ~890K faces in examples), meshy-6, meshy-6-lite
  (about a quarter of the cost, remeshed to 30K), Smart Topology meshy-t2.5 / t2 (triangles, 100-30K; Meshy recommends
  t2.5 for game characters to rig and animate).
- Texture: meshy-7 (PBR up to 8K, 1-4 reference images), meshy-n1 ("stylized", base colour, 2K), meshy-6 (+emission).
- Remesh: quad or triangle, 100-300K polys. `pose_mode`: `a-pose` / `t-pose`. No style presets (`art_style` deprecated):
  consistency comes from reference images (`texture_image_urls`) and Retexture.
- Typical times: mesh 4-90 s, textured 1-3 min, rig ~40 s, animation ~15 s, remesh ~2 min.

## Rigging and animation
- Auto-rig: humanoid bipeds only through the API (the web app also has a quadruped dog and a beta "Smart Rig", GLB only,
  no animation library). Input: textured humanoid, facing +Z, <= 380K tris, A- or T-pose with clear gaps at the limbs.
- `skeleton_template` (added 10 Nov 2026): `meshyrig` (default, 24 bones, cm), `mixamo`, `ue5_mannequin`,
  `unity_humanoid`, `blender_rigify_basic_human`, `maya_human_ik`, `max_biped`, `cc5_hd_neutral_base`, `daz_genesis9`;
  `include_fingers`. Non-default templates export in metres; the clip is named `retarget_clip`.
- A May 2026 test (before templates) retargeting to UE 5.7 SK_Mannequin needed manual spine/clavicle remapping and saw
  hip pivots 5-15 cm off (foot-IK sliding). Re-test with `ue5_mannequin`.
- Animation library: 680 clips in the public list (Fighting: weapon attacks 34, punching 36, blocking 18, getting hit
  10, dying 11, spell casting 12). Free plan: 20 clips; paid: 500+. No clip named "defeat" or "grapple" found.
  Text-to-Motion generates biped clips from a prompt (feeds the Animation API via `motion_task_id`).

## Our 30 duel poses -> Meshy sources (to verify clip by clip)
- From the library: idle_guard (Combat Idle), slash_high / slash_low (Left Slash and weapon attacks), parry_high /
  parry_low / block_cross (Sword Parry, blocking), dodge_left / dodge_right (Stand/Roll Dodge), stagger_back / kneel_hit
  (getting hit), fall_back (Knock Down, dying), cast_raise / cast_release (Mage Spell Cast), throw_hip (Over Shoulder
  Throw), victory (Victory).
- From Text-to-Motion: grab_reach, grapple_lock, defeat, taunt_beckon, command_point, feint_shoulder, rise, and anything
  the library lacks.
- Plan: sample each clip at its key frame into our named pose (joint rotations on the shared UE5 skeleton), so the
  procedural pose library and the IK contacts stay ours and any rigged figure can use them.

## API
- `Authorization: Bearer msy_...`; base `https://api.meshy.ai/openapi`.
- `POST /v1/{image-to-3d | multi-image-to-3d | remesh | retexture | rigging | animations}` and `POST /v2/text-to-3d`
  return `{"result": "<task_id>"}`; poll `GET .../:id` (PENDING / IN_PROGRESS / SUCCEEDED / FAILED / CANCELED) or stream
  `GET .../:id/stream` (SSE). Results in `model_urls`, `texture_urls`. Failed tasks are refunded.
- Webhooks (dashboard, up to 5, shared-secret header, at-least-once, out of order possible).
- Rate: 20 requests/s on self-serve plans; concurrent tasks Pro 10, Premium 30, Studio 60, Ultra 100.
- A paid plan is needed to create tasks through the API. Files are deleted after 3 days (Studio 14): download at once.
- Tooling: `meshy-cli` (npm) and an MCP server (`@meshy-ai/meshy-mcp-server`); no official language SDKs found.

## Credits per operation (API)
| Operation | Credits |
|---|---|
| Image / multi-image to 3D, meshy-7.1 or 6 | 20 mesh, 30 with 2K texture, 35 with 8K |
| Image to 3D, t2.5 or 6-lite | 5 mesh, 15 textured |
| Retexture | 10 |
| Remesh | 5 |
| Rigging | 5 |
| Animation | 3 per action (up to 10 per call) |
| Text-to-Motion | 10 (prime) or 3 (swift) |
| Text-to-Image | 3-9 |

Rough cost per demigod: 30-45 credits (clean views 3-9, mesh + texture 15-30, remesh 5, rig 5) plus 3 per clip.
With Chris's 2,275 credits that is about 40-55 rigged figures, or a 5-figure pilot plus a full pose set for ~200-300.
Unverified: whether subscription credits spend through the API or separately bought API credits are needed.

## Formats and Unreal
- Exports: GLB, FBX, OBJ, USDZ, STL, 3MF (Remesh/Convert also BLEND). Rigged/animated: FBX, GLB (+USDZ for animation).
- Meshy for Unreal plugin: a bridge from the web app into the open level (FBX into `Content/MeshyImports`), Pro plan;
  page versions disagree (5.4-5.8 vs docs up to 5.7); macOS needs a source compile. For us: plain FBX skeletal-mesh
  import onto one shared Skeleton, then the UE IK Rig / Retargeter.

## Licensing and privacy
- Free plan outputs: CC BY 4.0 (attribution). Paid plans: "you own the assets" (private, customer owned).
- Unverified: whether free-tier generations are public. Privacy controls are listed for Studio and Enterprise.

## Quality
- Characters are "passable base meshes": faces, hairlines and joint topology may need touch-up; outputs can be denser than
  needed (remesh) with occasional non-manifold edges. Texturing degrades when a reference image's shape differs from the
  mesh, so give it clean views of the same figure.

## Proposed pipeline (pilot 3-5 demigods first)
1. Clean views: Image-to-Image from the portrait + diorama + visual sheet to a clean A-pose front view with no base
   (plus side/back if needed). Our dioramas are figures on tiles, not true multi-views.
2. Multi-Image-to-3D, `pose_mode: "a-pose"`: meshy-t2.5 (~15K tris) or meshy-7.1 + Remesh (quad ~10-20K). Texture with
   `texture_image_urls` = portrait + diorama; meshy-n1 for one consistent stylized look across the cast.
3. Props and emblems as separate generations, attached to sockets.
4. Rig with `skeleton_template: "ue5_mannequin"` (+ fingers if the hands allow); fallback `mixamo` + one retarget map.
5. FBX import onto one shared Skeleton; our pose library + IK drive every figure.

## Open questions
(a) `ue5_mannequin` bone names, root and scale vs UE 5.8 Manny (four days old); (b) hip pivots / foot IK on small
figures; (c) API vs subscription credits; (d) the UE 5.8 plugin on macOS; (e) whether stylized tabletop proportions (big
heads, props in hands) rig cleanly; (f) free-tier visibility; (g) consistency across ~30 figures.

Sources: docs.meshy.ai (changelog, models, multi-image-to-3d, image-to-3d, text-to-3d, remesh, rigging, animation,
pricing, rate-limits, webhooks, task-lifecycle, asset-retention, authentication, ai), api.meshy.ai public animation
list, meshy.ai/pricing, meshy.ai/integrations/unreal-engine, help.meshy.ai (commercial use), strayspark.studio
(2026 auto-rigging and generative 3D comparisons), 3dprintingindustry.com (Meshy review), g2.com.

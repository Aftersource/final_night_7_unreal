# The duel in Unreal: what to build

Chris approved the browser duel and wants it in the game: when someone is challenged,
every client runs the whole Archon-style real-time typed fight, with masked typing, on the miniature stage.

The reference is the browser version: server repo `Aftersource/final_night_7`, folder `duel_lab/`
(`cd duel_lab && python3 -m http.server 8177`, open http://localhost:8177/; keys: space, R, arrows, D = debug).
Read its `README.md`, `duel.json`, `main.js` and `lib/`. Build the same thing in Unreal. The concept image is
`docs/design/challenge_box_concept_v1.png`; the skins are in `docs/design/mannequin_skins.png`.

The exact payloads are in the server repo `docs/CLIENT_API.md`: "Challenge mode (0.8.1)", "Duel panels (0.8.2)" and
"Exact resume: `present`". That document wins over this one wherever they differ.

## 1. What the player sees, in order

1. **Lock-in** (`challenge.lock`, for EVERY duel, including AI against AI): flash, flash, flash, a swoosh; both
   fighters' diorama portraits slam in from the left and right and lock; the camera glides to the fight (never a pop);
   the globe spins to the spot and dims. Don't interrupt a player who is typing or dragging: queue the glide, show the
   banner. When the duel ends, glide back to where their view was.
2. **The box**: a closed carved wooden box rises onto the table in front of the globe and opens like a clamshell.
   The lid is the backdrop (a sky for the place and time of day, a gilded arch reading CLASH, the two portraits as
   plaques with health bars). The base is a hex stage.
3. **The scene** (`duel.scene`, in `challenge.lock.also[]`): build every object from its `parts[]` (box, cylinder, cone,
   sphere, tetra, octa, ico, dodeca; metres) at its stage hex, with its material's look.
4. **The fighters**: two figure-drawing mannequins at `a_start` / `b_start`, each in its skin (section 3).
5. **Panels** (`duel.panel`): each plays from `starts_at_ms` for `play_ms` on the SERVER's clock. Per fighter, play
   `segs[]` in order: the named clip for `ms`, moving to hex `to` along `path` (dash, leap, knock, vault, slide,
   wallkick; later flip, wallrun, mantle, drop). At each `contacts[].t_ms`: the striking limb reaches the contact point
   (IK), a flash, a camera shake, the burst word (`text`), and the health bars move to `hp`. `flip: true` = an
   "INITIATIVE FLIPS" cut. `caption` in a comic caption box. `shot` picks the camera, which must keep both fighters in
   frame.
6. **The tray** (the box's front drawer): both fighters' typing, masked, and the window bar: it fills over the panel
   and the marker sits at `input_closes_ms`. After the marker: "LOCKED".
7. **The end** (`challenge.end`, the `ko` and `outro` panels): the winner's victory, the loser down, the result and
   the deed line; the box folds shut and sinks; the globe returns; the camera glides back.

## 2. Typing: the Archon part

- Only the two duellists type. A line is sent with `fields.duel {duel_id, text}` (Enter sends). Each line is resolved
  at once by the server (`duel.blow`); the panels then stage it.
- **Masked**: as a player types, each character shows for an instant and then becomes `*` (their own line and the
  opponent's line). Read it fast and you know their move.
- **Today** the opponent's line reaches you when it is sent, not per keystroke: reveal it masked in the tray with the
  same per-character flash over a few hundred ms, from `duel.blow.text` / the panel's `a.line`, `b.line`
  (`line_masked_len` is its length). **Next server release**: a live per-keystroke relay (`duel.typing`), so build the
  tray to take characters one at a time.
- **Pacing**: each duellist's client sends `fields.duel_tick {duel_id}` at every `input_closes_ms` (+ ~30 ms) by the
  server's clock (offset from `sys.pong.server_time` or `present.server_time_ms`). Viewers never tick.
- The duel lasts 30 s. The world keeps beating underneath for now (the engine's hold window isn't built yet).

## 3. The mannequin, skins and clips

All in `production_assets/meshy/mannequin/`:

- `mannequin_all_clips.glb` (14 MB): the mesh, the skeleton (UE5 mannequin bone names: pelvis, spine_01..03, neck_01,
  head, clavicle/upperarm/lowerarm/hand _l/_r, thigh/calf/foot/ball _l/_r; Y up, forward +Z, metres, 1.7 m tall) and
  all 35 clips as named animations. Import with Interchange as a skeletal mesh with animations; one shared Skeleton.
- `mannequin_rigged.fbx`: the same rig as FBX with 4K textures, if the FBX path imports better.
- The clip names are the server's vocabulary: `airborne_hit block block_cross bow combo_triple command counter defeat
  downed evade_roll evade_spin evade_stand grapple_throw_down ground_slam heavy_charged heavy_hammer idle_guard invoke
  jump_attack kick kneel lunge parry_back parry_high rise run shield_push stagger_back stagger_waist strike_side
  strike_slash taunt throw_shoulder victory walk`. The clips carry root motion in the pelvis: strip its X/Z (the
  panel's `to` and `path` move the body), as `duel_lab/main.js` does.
- **Skins**: `skins/{wood,obsidian,gold,dragonscale,butterfly}/texture_0_{base_color,normal,metallic,roughness}.png`
  (2K). They share one UV set: a skin is a material swap on the same mesh. Make one master material with those four
  maps and five instances.
- **Which skin a fighter wears**: until the server sends it (planned: `a.skin` / `b.skin` in `challenge.lock`), pick it
  from the demigod's name so everyone sees the same: `FNV-1a 32-bit hash of the lower-cased name, mod 5`, in the order
  wood, obsidian, gold, dragonscale, butterfly. Tint the stage ring and the plaque with the demigod's `aura`.
- **Skins mean weight**: in the lab a fighter's mass comes from its skin's material (gold 178 kg, obsidian 63,
  wood 43, butterfly 16) and changes jump height and how hard a blow lands on the scenery. Keep that table.
- **Next**: more skins come the same way (a Meshy retexture of the same mesh, ~10 credits each: stone, bone, ice,
  iron, coral...). Later each demigod gets its own Meshy figure on this same skeleton, so every clip still works.

## 4. Physics and IK

Port the lab's approach, not a different one: `duel_lab/lib/` has the material table, the mass properties, the
rigid-body step, the bake and the IK.

- Objects have real mass from their parts and materials. Contacts with `on: <object id>` hit the scenery with
  `impulse`.
- The lab BAKES its physics deterministically (120 Hz, fixed step, seeded), so rewind and resume are exact. In Unreal
  do the same: simulate with a fixed step from the panel data and keep the result per panel, or port the lab's small
  engine. Don't hand the scene to Chaos with a variable step: two clients, and a client that reconnects, must show
  the same thing.
- IK: two-bone IK on both arms and legs after the clip is evaluated: feet on the surface under them, hands on vault
  and wall contacts, the striking limb on the contact point at `t_ms`.

## 5. Coming back to the exact moment

`match.snapshot.public_state.present` (and the `game.present` query) carries `server_time_ms` and, for each open duel,
its `scene` and every panel so far. On join, reconnect or relaunch: build the stage, find the panel that contains
`server_time_ms`, and seek into it by `server_time_ms - starts_at_ms`. Everything on the stage must be a function of
the duel time, as in the lab, so seeking is exact. A viewer whose socket dropped must send `room.join {viewer: true}`
again right after the `auth.ok` of its resume.

## 6. Who builds what, and the order

| Step | Owner | Done when |
|---|---|---|
| A. Import the mannequin, the 35 clips, the 5 skin materials; one mannequin on the table cycling every clip in every skin | Mac (`mac/duel-*`) | a render + Chris's live screenshot |
| B. The box: rise, open, lid backdrop, plaques, hex stage; the scene builder from `duel.scene` | Mac | the lab's Jakarta scene (from `duel_lab/duel.json`) built in Unreal |
| C. The panel player: segs, paths, contacts, bursts, captions, shots, health; driven by a local copy of `duel_lab/duel.json` first | Mac | the lab's 31 s duel plays in Unreal, scrubbable by a debug time |
| D. Live: `challenge.lock.also[]`, `duel.panel` events, `duel_tick`, the typing tray, `challenge.end`, `present` seek | Mac, with the net calls in `FNNetClient` | a real duel on STABLE, both duellists and a viewer |
| E. Windows: build, run, the same duel on Benito's PC; the packaged build | Benito's bridge | a screenshot from Windows; in the package |
| F. Physics bake and IK parity with the lab | Mac | the crate, the glass and the pillar behave as in the lab |

Steps A to D are the jam's must-have. E follows each merge. F is as far as time allows: without it, play scenery hits
as a shudder and a burst.

Proof for every step: a full unity build, a render checked by eye, and a live screenshot, as always.

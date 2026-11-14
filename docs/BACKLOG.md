# Final Night 7: the client backlog (ordered by the backend)

Take the top unchecked item for your owner (PLAYBOOK Play 11). Prove each item with a screenshot under
`docs/proof/` on your branch. The backend reorders this list; check it after every merge announcement.
Server data that's still to come is marked **(needs server 0.7.0)**: build against the documented shape and
fall back gracefully until it ships.

## Benito's side (`benito/*`): bridge plus Windows builder
1. [ ] **Aura colour per player, everywhere.** Globe pins, every board level, minifigs, seat aura and cards. Use the server `aura`
   `"#RRGGBB"` when it's present, otherwise a stable seat palette. Both of a player's demigods share it; an L/R notch marks the side.
   Human is a steady glow, AI a flicker or cube aura, fallen is grey.
2. [ ] **Viewer state plus TAKE A SEAT** (house attract mode is live): a public-only view, a TAKE A SEAT button
   (`TakeSeat()`), and `house.forked` → `seat.joined` → `match.snapshot` handled; resend a submit that got wrong_phase in Compose.
3. [ ] **Two demigods per player (needs server 0.7.0):** 8 figures on a 4-player table, two lower-left cards per
   player, each with its own health, alive state and portrait. Use the Mac kit's SFNPortraitCard and SFNPanel once merged.
4. [ ] **The final wish (needs server 0.7.0):** round 1, `round.brief.public.final_wish: true`. A big "MAKE YOUR FINAL WISH,
   0:30" moment with the WILL scroll.
5. [ ] **A globe click opens zoom 2 on the region's hotspot (needs server 0.7.0):** `game.globe regions[].hotspot`.
6. [ ] **Rules visuals:** the Time bill and debt meter on the cards; the Entropy meter (0–100, level thresholds) plus a
   heat-death screen; hexes outside Time's circle go dark on the boards; armies, mobs and world mods as pieces or
   tinted hexes; a streak arc from caster to target.
7. [ ] **Procedural minifigs** from visual traits (12–20 faceted primitives, cloak in the aura colour, emblem decal, prop
   primitive), using `traits_ids` when the server adds them.
8. [ ] **Pieces placed by `cell`** (the people-level hex id) once the server ships it (needs server 0.7.0).
9. [ ] **Clean out Director's Cut leftovers:** NOTES.md and IDEAS.md (pre-port), the project name in DefaultGame.ini.
10. [ ] **A Windows screenshot of live portraits** for `docs/proof/portraits_live_windows.png`.

## The Mac session (`mac/*`): the UI kit (Source/FinalNight/UI/**) plus the board area of effect (by Chris's order)
1. [ ] **Push `mac/visual-style`** (waits on Chris's approval on that Mac).
2. [ ] **Switch JOIN THE GAME to `Net->JoinHouse()`** (it's on main since a30faf2); drop the FN7JoinCode.txt write.
3. [ ] **Board area of effect:** single-hex snap and cast-on-drop (UI/FNBoardAoe.cpp plus the two FNStudio.h declarations).
4. [ ] **The WILL area as an old fabric scroll:** procedural woven cloth, torn edges, rods, ink; unrolls as the game begins.
5. [ ] **Power tiles reorderable within their panel; HUD panels draggable with snapping.**
6. [ ] **A viewer-state HUD** in SFNHudLive: hide the WILL and the powers, show TAKE A SEAT.

## Backend (`final_night_7`), for reference
- Rules 0.7.0: two demigods per player with their own health, items and manifesting, `cell` plus the hex rule, `aura`, the live
  Time bill, crowd/mob positions, the final wish, hotspots and AI drift. Then a Play 8 release.

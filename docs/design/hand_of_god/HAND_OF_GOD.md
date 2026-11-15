# The hand of god: grab the planet, grab the land

Chris proposed a hand-grab, rotate-the-planet style control, with a double click to zoom in and a right click
to zoom out, but it must not do that when the pointer is over the extended click zone of one of the demigods, since that was frustrating.
ABOUT THAT RIGHT". Then: "just make it happrn man hjust get it fesgned for out unreak gusys".

Concept: `hand_of_god_concept.png` (the four cursor states and the two grabs). The look is a pale marble hand with a gold
filigree cuff and a faint gold edge glow, the same stone as the gods' statues. Owner: the Mac session (mac/hand-of-god).

## 1. The cursor

> **Superseded (2026-11-15, Chris): the drawn-hand control looked and felt poor and was too laggy, so it should stop.
> Use the mouse pointer with proper controls instead.** The drawn hand is removed (it was
> redrawn at the game's frame rate and trailed the real pointer). The game uses the system mouse pointer; the controls
> below are unchanged. The table below is kept as the record of what was tried.

| State | When | Sprite |
|---|---|---|
| OPEN | Over the globe or empty board, nothing pressed | Open palm, faint glow |
| GRAB | Mouse held and dragged past the threshold on the globe or board | Closed fist, brighter glow, 3 tick marks |
| POINT | Over a demigod's extended click zone, a card, a power hex, the pouch or a panel | Index finger |
| FLING | 0.25 s after a release with speed | Open palm with a short gold comet trail |
| (OS arrow) | Inside text boxes (WILL, the duel tray) | The system cursor while typing |

- Hotspot: the palm centre for OPEN, GRAB and FLING; the fingertip for POINT. Size 48 px at 1080p, scaled with DPI.
- Software-drawn in Slate, on top of everything. The OS cursor is hidden over the 3D view. Crossfade states in 80 ms
  with a small squash (scale 1.0 → 0.9 → 1.0) on GRAB.
- Cut the four sprites from the concept, or redraw them as vectors in the kit's style, white and gold.

## 2. Grab the planet (the globe)

- **Start:** press on the globe and move ≥ 6 px (DPI-scaled) within the press. Under 6 px stays a click or double-click.
- **The rule:** the planet point under the hand stays under the hand.
  - On press, ray-cast the cursor against the globe sphere (centre C, radius R) to get the hit P0, and store
    `a = normalize(P0 - C)` in the globe's local space.
  - Each frame, ray-cast again to get `b`. If the ray misses the sphere, use the closest point on the silhouette.
  - Apply `Q = FQuat::FindBetweenNormals(a_world, b_world)` to the globe: `GlobeQuat = Q * GlobeStartQuat`. Always
    recompute from the press-start quaternion, never by accumulating, so there's no drift.
- **Fling:** keep the last 100 ms of angular velocity, `ω = axis × angle / dt`, from the frame quaternions. On release
  with |ω| > 0.3 rad/s, coast: each frame rotate by ω·dt and decay with `ω *= exp(-dt / 0.6 s)`; stop below 0.05 rad/s.
- **Catch:** a new press during a coast stops it at once and starts a new grab.
- No Euler angles anywhere. The star world (FNStarBackdrop) already follows every globe quaternion change.
- The table board follows the globe's front-centre point the way the centring slerp does now. Re-centre the board only
  when the centre crosses into a new hex (IsSameBoardSpot), never per frame: no rebuild storm.
- Log: `FN7 hand: grab globe at lat,lon` / `FN7 hand: fling w=1.8 rad/s` / `FN7 hand: caught`.

## 3. Grab the land (the table board)

- **Start:** press on empty board (not a figure's extended zone, not a power hex, not a panel) and move ≥ 6 px.
- **The rule:** the land under the hand moves with the hand.
  - Project the cursor onto the board plane at press (L0, as lat/lon through the board's mapping) and each frame (L1).
  - Shift the board's centre by L0 − L1 in lat/lon so the grabbed spot stays under the hand. Use a great-circle shift
    at the regions level; plain metres are fine at finer levels.
- **Display:** translate the existing tiles visually while dragging (the BoardRoot offset). Rebuild only when the
  centre has moved a whole hex, and then flow it with the zoom ghost (no pop, no empty frame). The globe turns to keep
  the board's centre at its front-centre, through the same quaternion path.
- **Fling:** the same rule as the globe, in metres per second, decaying with tau 0.5 s.
- Log: `FN7 hand: grab board` / `FN7 hand: pan to lat,lon` / `FN7 board rebuilds: N in the last minute`.

## 4. Zoom, and where nothing happens

| Input | Empty globe or board | Inside a demigod's extended zone | Over UI (panel, ring hex, pouch, duel box) |
|---|---|---|---|
| Press + drag | Grab (above) | Your own demigod: SUMMON drag. A rival: nothing | The UI's own drag |
| Click | Nothing moves (the old rule) | Select | The UI |
| Double-click | Zoom IN one level at that point, animated | Centre on the demigod; a rival in your hex = challenge | The UI |
| Right-click | Zoom out one level, animated | Zoom out one level, animated (Chris asked that right click zoom out without losing track of the view, the same way) | The UI |
| Wheel | One level per gesture at the cursor (centred on the nearest demigod if within its zone) | Same | The UI scrolls |

- The extended zone is `AFNTableGods::FigureAtScreen(..., ZoneScale)`, the same one WhyBlocked uses: the figure's own
  click zone, ×1.35 for near misses.
- Right-click is never eaten by a demigod; only real UI under the cursor keeps it. Its log: `FN7 zoom: out (right-click)`
  or `FN7 zoom: out (right-click) near dg:N`.
- Every blocked gesture logs its rule: `FN7 zoom: blocked by figure dg:N at D px | duel box up | power hex N | pouch |
  panel <id>`.
- All zooms animate (the zoom ghost and rise-from-flat). Nothing pops.

## 5. Proof before push (the rule)

1. Chris's editor log shows a globe grab, a fling, a catch, a board grab and pan, a double-click zoom in, and a
   right-click zoom out (one of them near a demigod).
2. Frames or screenshots of the globe mid-grab (the grabbed spot under the hand) and of the board mid-pan.
3. Then push mac/hand-of-god. The backend merges, and Benito's PC builds both targets and proves it from his log.

## 6. LOCKED: the panning Chris approved (2026-11-15 04:58 UTC)

Chris confirmed that this works and asked for the main session to keep it as it is. This is the build at Unreal
main 1c71497 (mac/pan-smooth 3905e99). Every future change, Benito's side and the Mac's, is held to these rules:

1. The land slides under the hand for the whole drag (MapPivot translation). The board re-centres seamlessly every
   1.5 hexes (~1 ms each, measured) so the far edge is always full, and once on release. (The 11-hex rule left half
   the board empty and made the edge "teleport": Chris asked for the edge to teleport further.)
   A re-centre must never move the land under the hand.
2. While panning, the relief keeps its scale (bReliefHold) and the board stays on its cube face (no face swap at a
   seam). A zoom or a centring releases both.
3. Nothing else touches the board under a moving hand: earth playback holds its answers until release
   (AFNStudio::IsBoardPanning), no rebuild storms, no coast or fling on the board.
4. The system mouse pointer; no drawn cursor.
5. The land under the pointer stays under the pointer: no camera motion during a grab (the player controller's lean
   toward the mouse holds still while AFNStudio::IsHandHolding()), and the grab plane is the pressed hex's height
   (HexAtScreen's top). Chris noted that when the mouse hovers over a tower, the tower
   needs to be on the mouse when it moves. Measured: the pressed hex stays within 3 px of the pointer.

Any change that adds a rebuild, a re-normalisation or a face switch during a drag breaks it again. Changes that touch
input, pan, zoom or the board rebuild path get a 60-second live drag-and-zoom test in Chris's editor at her real fps
before push, with the log lines in the report.

Rule: a click on the globe goes where you click. A press that moves under 20 px (1080p-scaled) within 0.6 s is a
click, not a grab: it undoes any tiny turn and runs the globe pick (dive to that region). Chris reported that the
globe single click does not work.

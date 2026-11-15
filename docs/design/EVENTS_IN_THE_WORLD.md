# Events in the world

Benito approved the defaults on 2026-11-15. Written with #benito/gamedesign from his input. This is the single source for the Mac, the server and Benito's side.

Benito described wanting world events to play out: when he drops a flood or sets a wildfire, he wants to see forests burn down to nothing, a hurricane hit a village so people run to shelters and shelters can be destroyed, and typed in-game actions, such as a character charming a villager, acted out with the token pieces.

## The rule

What you do changes the WORLD, not the log. At every zoom (globe, regions, villages, houses, people) you see the damage at that scale, and it PERSISTS. The picture must match the server's numbers: 1,400 dead means that many fallen tokens at people scale, scaled down per level.

## Flood

- Globe: a dark-blue stain that stays.
- Regions: hexes sink to water, outward from the drop.
- Villages: the water climbs terraces, low villages go under, roads are cut.
- Houses: the water rises between houses; houses tilt, float or sink; trees bob.
- People: tokens are swept away (they lie down); survivors stand on roofs and high ground.
- After: mud-brown hexes and wrecks for several beats, drying.

## Wildfire

A flame front moves tree by tree. Each tree becomes a black stump on grey ash ground, with smoke. People and animals flee. If everything burned, nothing is left; if only part burned, exactly that much. It regrows slowly over many beats.

## Hurricane on a village

A spinning cloud crosses the board. People tokens RUN TO SHELTERS. Roofs fly off. If the storm is strong, shelters break and people scatter or fall. The wreckage stays.

## Typed will actions

For example, "I go into the house to my left and seduce the maiden", played out with the little tokens:

1. The camera drops to people level.
2. Your demigod token walks to the house on its left.
3. The roof lifts like a dollhouse; a maiden token is inside.
4. Speech bubbles and hearts; the d20 tumbles beside them.
5. The outcome plays: a heart burst, or a slap and she runs out.
6. The camera returns.

## Defaults (approved)

1. The camera stays put for other players' disasters. For YOUR own, it glides over to watch, within HAND_OF_GOD.md §6: never while grabbing, zooming or typing, and it always returns.
2. About 4 s per disaster and about 8 s per typed action.
3. Damage persists and heals slowly: the forest regrows, floods dry.
4. Typed actions always zoom into the scene, then return.

## Ownership (agreed with the backend, 2026-11-15 01:35)

- **gamedesign (Benito's side):** board-life reactions in FNStudioTable, driven by HexStateAt() and per-cell counts: stumps on Burning/Ash, sinking and tilting houses on Flooded, roofless and wrecked houses, fleeing and fallen people tokens. Keep §6: no BuildTable under a moving hand, nothing while a zoom animates, per-beat work only between gestures, no input or camera changes.
- **Server:** earth.effects[] with per-cell counts (building / forest / crowd / road / shrine / person; burned / flooded / destroyed / fled / sheltered / died, with to_cell and counts), and token-play scenes (actors, place cell, timed steps, captions, outcome, d20). Contracts in CLIENT_API.md.
- **Mac:** the camera glide for your own disasters and typed actions (within §6, returning the view), and the token-scene player (the dollhouse roof lift, bubbles, d20, outcome).

Data that already exists: game.earth {cell} at any level (docs/EARTH_SIM.md in the server repo), the Mac's FNEarthPlay, and the height/state hook (AFNStudio::SetHexOverride / BoardHeightAt / HexStateAt).

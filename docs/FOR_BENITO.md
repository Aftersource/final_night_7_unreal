# Benito: read this first

Written 14 Nov, late afternoon EST, while you slept. The build is due **15 Nov, 8:59 PM EST**. Late is disqualified.

## 1. Look at these three things (10 minutes)

1. **The miniature battle.** Open http://localhost:8177/ on your PC (your bridge agent has it running; if not:
   in the `final_night_7` repo, `cd duel_lab`, `py -m http.server 8177`). Press space to pause, R to replay, drag the
   slider to rewind, and use the two dropdowns at the bottom right to change each fighter's skin. Press D for the
   debug view.
   This is challenge mode: a wooden box opens on the table, and two figure-drawing mannequins fight a 30-second
   duel in comic panels, with parkour, real physics and things that break. Chris approved it and it is now being
   built into Unreal.
2. **The concept and the skins:** `docs/design/challenge_box_concept_v1.png` and `docs/design/mannequin_skins.png`.
3. **Your eight features in the game.** All eight are merged on main: settlements that grow, the piece carrying its
   story, the d20 tumbling on the table, encounters, loot, day and night, roads, shrines. Your bridge agent built
   them while your other sessions were silent. Run the game and check each one looks the way you meant.

## 2. What happened while you slept

- **One always-on game.** Everyone joins the house game FINAL7 on Chris's server. Click the bag to join.
- **Challenge mode is live on the server.** Double-click a rival: a confirm box, your demigod teleports beside them
  if it has to (it can cost 1 to 2 health), then a 30-second duel of typed lines. Each line is judged at once by the
  rules and a d20. The server now also sends the stage and the timed comic panels for the duel.
- **You come back to the exact moment.** Quit or crash, even mid-duel, and the server hands your client the whole
  present moment again.
- **The Dungeon Master's rulebook** is written from your words: `final_night_7/docs/DM_RULEBOOK.md`. The memory by
  place and time is built, and your eight features run on it.
- **Summon** (drag your demigod anywhere), **god-tier powers** (one per god), **fault lines** for mega powers.
- **Accounts:** guests now, Google sign-in later, and each player can write their two demigods' personalities. The
  player's words are kept exactly unless they break the content rules.
- **Meshy:** Chris shared her key with your side (it is not saved on your PC yet: your machine blocked the write, so that is your call). The mannequin, its five skins and 35 fight clips came from it.
  1,126 credits are left. Nothing is spent without you or Chris saying so.

## 3. Being built right now

| What | Who |
|---|---|
| The duel stage in Unreal: the box, the mannequins, the panels, the masked typing | Chris's Mac session |
| The same on Windows, and the packaged build | your bridge agent |
| Seeing the opponent's typing live, character by character, masked as `*` | the server |
| Wills that matter: healing and arming with real effects, resurrection, curses, mystic quests | the server |

## 4. Where your design eye is needed

Say it in your own words to your bridge agent. It reaches Chris's server session word for word.

1. **The duel's look.** Is the box right? The stage? The mannequins? Too dark, too busy, too slow? What should a hit
   feel like?
2. **Skins.** There are five: wood, obsidian, gold, dragonscale, butterfly. Which others do you want (stone, bone,
   ice, iron, coral)? Each costs about 10 Meshy credits. Should a skin be earned, chosen, or come from the demigod's
   story? In the browser version a fighter's weight comes from its skin: gold hits hardest and jumps lowest.
3. **The moves.** The duel knows these classes: strike, heavy, guard, parry, feint, evade, close, retreat, grapple,
   throw, invoke, command, taunt. What is missing? What should beat what?
4. **Scenes.** Each duel's stage is built from the place it happens: pillars, crates, a desk, a shrine if one stands
   there. What should a harbour, a desert, a forest or a great city always have?
5. **Balance.** Every number is a setting: duel length (30 s), teleport risk, bolt and spark costs, how often the AI
   challenges. Play, then tell us what felt wrong.
6. **Quests and curses.** The DM will hand out mystic quests ("the wings of a butterfly that crossed the strait...").
   Give us ten of your own in that voice.
7. **Your eight features.** Anything that looks wrong is yours to call.

## 5. Decisions only you and Chris can make

1. **The team name.** The zip must be named `TeamName_GameName`.
2. **Each member's name and Epic account email**, for the submission form.
3. **Who records the 30 to 60 second gameplay video.**
4. **How the shipped game reaches the server.** The packaged build has no access key, so a judge cannot connect yet.
   The recommendation is a separate player key baked in, with spending caps, changed after judging.
5. **Your two editors are open on your PC.** Your bridge agent will not close them in case of unsaved work, so it
   cannot run the game there. Close them when you can.

The jam's rules, the packaging steps and the full checklist are in `docs/PACKAGING.md`. The duel's build plan is in
`docs/DUEL_UNREAL.md`.

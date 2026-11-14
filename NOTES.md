# Final Night: Unreal front end (Director's Cut)

## How to run
1. Double-click `FinalNight.uproject`. If Unreal asks "rebuild missing modules?", click **Yes** (needs Visual Studio C++).
2. Press **Play**. The game starts in **DEMO REEL** mode: a scripted session that needs no server and no AI key.
3. To use the real server: open `Config/DefaultGame.ini` and set `bDemoMode=False`. Then set `ServerUrl` to `ws://localhost:4321` (local) or the hosted `wss://` address.

## Screen flow (theme: Director's Cut)
| Screen | What happens | Protocol |
|---|---|---|
| Title: "ACTION!" | start | connect: Alive → Authenticate → Login → RequestAvailableGames + EnterGeneralLobby (automatic) |
| Casting call | pick a reel (game) → "TAKE YOUR MARK" | JoinInGameLobby {gameUUID} (object) → InGameLobbyJoined; JoinGame (raw string) → GameJoined |
| On set | welcome text (typewriter), cast list with locations, **Director's note** = your WILL → "LOCK THE TAKE" | GameWelcomeMessage, SendAllPlayerStates; SetWILL (JSON string {gameUUID, playerWILL}) |
| Rolling | scene text (primer), props found with confidence bars, two takes (up/down episodes) → "PRINT THIS TAKE" | SendRoundPrimer, SendPlayerFoundItems, SendPlayerEpisodes; **SelectEpisode** (new) |
| Next take / That's a wrap | loop rounds; "Final Cut" = reel of every WILL + the take you printed | n/a |

Persistent chrome: letterbox bars, blinking REC, scene/take header, running timecode, film-strip sprockets, and a status line with a DEMO/LIVE tag.

## Needs a backend change (for Adorial/Chris)
The 2024 server has **no episode-selection message**, so every round waits 15 minutes. The client already sends:
```
{ "type": "SelectEpisode", "data": "{\"gameUUID\":\"default\",\"round\":1,\"episodeUUID\":\"...\"}", "token": "<jwt>" }
```
The server needs to: add `SelectEpisode` to `MessageType.ts`, route it to `GameExecutionEngine` to resolve the "wait for episode selection" step, and implement EndRound/NextRound so round 2 starts. Also fix `hasGameStarted` (`||` → `&&`) so a second human can join.

## Files
- `FNNetClient`: WebSocket + envelope + double-parsed `data` + handshake + reconnect + offline demo server
- `SFNRootWidget`: the entire UI in Slate C++ (no Blueprint assets, so it merges cleanly in git)
- `FNGameMode` / `FNPlayerController`: boots the UI on the default Entry map

## Pre-jam material (declare in the submission)
Final Night 2024 backend protocol and design (aftersource/finalnight, elationate/finalnight-unity), used as reference.

## Claude mode (local testing only)
With `bDemoMode=True` and `bClaudeMode=True`, each round is written live by Claude (`ClaudeModel`, default Haiku) from what you actually type. The key lives in `Config/ClaudeKey.txt` on this PC only. **Never commit it, and never ship a build with it.** Judges must use the hosted server. If the key is missing or a call fails, the round falls back to the scripted take.

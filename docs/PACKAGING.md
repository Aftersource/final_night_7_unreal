# Packaging Final Night 7 for the Epic AI MegaJam

This is the runbook for the jam deliverable: a packaged Win64 build of this project, zipped and uploaded to itch.io.
**Due: Nov 15 2026, 5:59 PM PST / 8:59 PM EST (Nov 16 01:59 UTC). Late entries are disqualified.** After Nov 15,
12:00 PM EST only blocking fixes ship (PLAYBOOK §6).

Dry run done on 2026-11-14 on Benito's PC (UE 5.8, VS 18 toolchain 14.52): **BUILD SUCCESSFUL**.
It took 8 min 20 s: compile 402 s, cook 83 s, pak 7 s. The output was **575 MB** (`FinalNight.exe` plus one pak).

## 1. Build it

Close the editors first if you can. If you can't, keep `-nocompileeditor`: BuildCookRun also rebuilds the editor
target, and that fails with "Unable to build while Live Coding is active" while any editor or game has Live Coding on.
With `-nocompileeditor`, the cook uses the editor binaries already built. Build the editor once (or launch it, which
builds it) after pulling new code.

From a Windows `cmd` (one line):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="C:\Users\User\Documents\Unreal Projects\FinalNight\FinalNight.uproject" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="C:\Users\User\Documents\FN7Package" -utf8output -unattended -nocompileeditor
```

- The jam page recommends **Development** builds; Shipping is also accepted for Windows. The dry run used Shipping.
  Use **Development** for the submission (it has a log, and it's what the page recommends). Switch to Shipping only if
  Chris asks.
- Success ends with `BUILD SUCCESSFUL` / `AutomationTool exiting with ExitCode=0 (Success)`.
- The output is in `C:\Users\User\Documents\FN7Package\Windows\`: `FinalNight.exe`, `Engine\`, `FinalNight\`
  (`Content\Paks\FinalNight-Windows.pak`) and the three `Manifest_*.txt` files.

## 2. Secrets check (every build, before zipping)

The rule: no key of any kind ships in the client (PROJECT_GUIDE §1, README). All LLM and image work runs on the server.

1. `Manifest_UFSFiles_Win64.txt`: the only project config files should be `FinalNight/Config/DefaultEngine.ini`,
   `DefaultGame.ini` and `DefaultInput.ini`. **No** `Config/*.txt`: no `FN7Key.txt`, `ClaudeKey.txt`,
   `OpenAIKey.txt`, `FN7DisplayName.txt`, `FN7JoinCode.txt` or `FN7ResumeToken.txt`.
2. Search the whole output folder for the bytes of each local key. Every search must find nothing. From WSL:
   `grep -rlF "$(tr -d '\r\n' < Config/FN7Key.txt)" /mnt/c/Users/User/Documents/FN7Package`.
3. `Config/DefaultGame.ini`: `bDemoMode=False` and `bClaudeMode=False` (Claude mode must never ship).

The 2026-11-14 dry run passed all three.

## 3. The access key (OPEN: Chris decides)

The client connects with `wss://…/ws?key=<access key>`. It looks for the key in this order: `AccessKey` in the
config, then `Config/FN7Key.txt`, then the `FN7_KEY` env var. The package carries none of them, so **a jam player's
copy cannot connect as it stands**. Chris is deciding the fix. The backend recommends a separate low-privilege
**player key**: house-only, with server-side spend caps, rotated after judging, and baked into `DefaultGame.ini`
`AccessKey`. **Bake nothing until Chris confirms.** Once she does, record the exact steps here and re-run the secrets
check: the player key is the one expected exception.

## 4. Zip and upload

- Zip the **contents** of `FN7Package\Windows\` as **`TeamName_GameName.zip`** (e.g. `<Team>_FinalNight7.zip`).
  The team name is still to be confirmed with Chris and Benito.
- It must be **under 1 GB** (the dry run was 575 MB unzipped). For a bigger build, use butler or the Blendo Itch
  Uploader.
- Upload it to the team's own itch.io account (a project page for the game), then submit that project to the jam:
  https://itch.io/jam/2026-epic-ai-megajam
- Test the zip on a clean folder: unzip it, run `FinalNight.exe`, join the house, see a beat, quit.

## 5. Jam submission checklist (from the itch.io jam page, read 2026-11-14)

Required:
- [ ] Unreal Engine track only (not UEFN), with a **packaged** build (unpackaged projects are refused), Windows 10/11
- [ ] Zip named `TeamName_GameName`, under 1 GB
- [ ] Uploaded to our itch.io account and submitted to the jam **before Nov 15 5:59 PM PST / 8:59 PM EST**
- [ ] **Team name**, plus each member's name and the **email of their Epic Games account** (up to 5 people, 18+, an
      active Epic account each)
- [ ] **Game name**
- [ ] **Gameplay video link, 30–60 seconds**
- [ ] **Declare everything made before the jam** (Nov 12): the 2024 Final Night prototype (Node backend + Unity
      client) and Final Night 2.5 (the earlier Theatre plugin), plus any pre-jam prompts, scripts and agent setup
      (`planningnight/BROTHER_HANDOFF.md`: "list it as pre-jam material in the submission")
- [ ] **List third-party assets and their licenses**: Natural Earth coastlines and cities (public domain); PB2002
      plate boundaries (Bird 2003, credit line in `docs/data/README.md`); engine basic shapes; Meshy-generated
      meshes and textures (`docs/MESHY.md`); any Fab, audio or font assets. Note: WeLoveIndies audio is
      non-commercial only.
- [ ] Theme **"DIRECTOR'S CUT"** ("Your vision, your edits, your final cut"): say how the game answers it
- [ ] No content that breaks the Official Rules (others' IP, inappropriate material)

Optional, but worth it:
- [ ] Dev log link (needed for the "Glass Box" modifier): BUILDLOG.md / an itch.io devlog post
- [ ] Tools used (needed for partner prizes, e.g. Hyper3D's)
- [ ] OpenAI Awards: list the OpenAI models and products used (the server's image generation) and how
- [ ] NVIDIA RTX Award: only if we use RTX features

Judging (each 1–5): ambition and innovation, theme, fun, gameplay, visuals, level design and progression, audio.

## 6. Order of work on the last day

1. Pull main, build the editor (or launch it once), run §1, then §2.
2. Run the client from the package: join the house as a watcher, see three beats, check that the 8 DM-rulebook
   features show, then quit.
3. Zip (§4), upload, fill in §5, submit, and confirm the entry shows on the jam page.
4. Keep the zip and the exact commit hash in BUILDLOG.

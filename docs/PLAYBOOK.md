# Final Night 7: the playbook (Chris's backend ↔ Benito's frontend)

> Copy of `Aftersource/final_night_7/docs/PLAYBOOK.md` (the source of truth), kept in sync by the backend session.
Read this whole file before acting, and again after every compaction. It is how the two teams work together.

## 1. The team
| Who | Owns | Address |
|---|---|---|
| **Chris** (designer) | The game's direction. Final say on everything. | through his session |
| **Benito** (designer) | Design, balance, the exact UX, playtesting, rule design | through his sessions |
| **Backend session** | The Theatre engine integration, Final Night 7's simulation, LLM minds, rules, storage, recording, hosting, server releases | `#chris/final-night-7` |
| **Bridge** (Benito's agent) | The client (browser, then Unreal), and the intermediary between Benito and the backend. Runs the server, simulator and admin tools on Benito's behalf. | `#benito-linux/finalnight-linux` (to be renamed `final-night-7-benito-bridge`) |
| **Mac verifier** (Chris's other computer) | Second Unreal build and verification of the client on macOS, as a cross-platform check; reports to the backend session; never pushes unless instructed | `#chris-biggun/finalnight-unreal` |
| **Windows builder** (Benito's PC) | Builds and runs the Unreal client on Win64 (the shipped platform); takes commands from the bridge | `#benito/where-were-we` |
| **Shared ground** | Image, video and character generation: designed together | both |

**Repos:**
- `Aftersource/theatre`: the engine (owned by the theatre_worker session).
- `Aftersource/final_night_7`: the server, rules and docs (backend).
- `Aftersource/final_night_7_unreal`: the Unreal client (committer of record: the bridge).
- `Aftersource/final_night_7_web`: the browser prototype (frozen).

The platform is **The Theatre**. The game is **Final Night 7**. The designers' words are kept verbatim in
`BUILDLOG.md`, which is the source of intent.

## 2. Two servers: STABLE and DEV
- **STABLE**: the URL Chris gave you (behind the access key). This is the calm version Benito playtests and designs
  rules against. It changes **only** through an announced release (§4). It is never touched mid-playtest without
  warning.
- **DEV**: the backend's private servers and the `main` branch. These churn constantly; don't build against `main` mid-day.
  Build against the **release tag** that STABLE runs.
- **Releases are git tags** on `Aftersource/final_night_7`: `playtest-N`. STABLE always runs exactly one tag.
  `GET /stats` and `server.hello` report the rules version; each release note gives the tag, rules version and engine
  commit.

## 3. The plays (how each kind of work flows)
**Play 1: a bug.** Bridge → backend, with this template:
```
BUG <short title>
server: STABLE (playtest-N) | room code: XXXXXX | match id: <from /admin/matches> | beat: N
expected: ...   got: ...   frames/queries: <the exact request and response, trimmed>
severity: blocks-playtest | wrong-but-playable | cosmetic
```
The backend answers within the hour with an ETA. A fix ships in the next release unless it blocks a playtest, in
which case it gets a hotfix release, still announced.

**Play 2: a change request** (new field, query, rename, knob or rule). Bridge → backend: what Benito wants, why (the
design intent), and the exact payload shape the client wants. The backend replies yes / no / alternative, plus which
release it lands in. Field renames are cheap; ask first.

**Play 3: balance.** Benito tunes through room settings: every setting is a knob, listed by `GET /sim/settings` once
the simulator API is live. Workflow: play with knobs on STABLE (`llm:false` for speed) → run batches with
`POST /sim/run` / `POST /sim/compare` → send the backend the settings Benito likes. The backend makes them the defaults
in the next release. Never change code to balance; change settings.

**Play 4: a release** (backend → bridge).
1. Both test suites are green (backend: `cargo test --workspace`; bridge: the repo suite plus `client/fn7`).
2. The backend verifies the build on a private DEV server, then **announces before deploying**, with the tag, the
   changes, and any payload changes (before → after).
3. The bridge replies "ok" (or "hold": Benito is mid-playtest).
4. The backend deploys to STABLE and verifies through the public URL: a full match, the queries, admin.
5. The bridge pulls the tag and confirms its client still passes.

**Play 5: the bridge pushes.** The bridge may push to `Aftersource/final_night_7` **only when the backend explicitly
instructs it**, and then only what was asked:
- on a branch `bridge/<topic>` unless told otherwise;
- with a commit message in the bracket style that names what changed, short and abstract (e.g. `[ lobby join and host wait ]`): one distinct message per commit, never a reused catch-all;
- never to `main`, never tags, never force-push.

Client code lives in Benito's own repo, not ours.

**Play 6: secrets.** The access key and admin token live in environment variables (`FN7_KEY`, `FN7_ADMIN`). Never put
them in a repo, a log, a screenshot or a message to anyone but the backend. If one leaks, tell the backend at once; it
rotates them.

**Play 8: the client follows every server release** (the orchestration loop). Every engine or server change that
reaches STABLE, and every client change, runs this loop so the Unreal client never drifts from the server:
1. **Backend → bridge:** the release announcement (Play 4) lists payload changes and "client action needed". For
   engine updates, the backend also lists any protocol change (new or changed frames, fields or hooks).
2. **Bridge → backend:** before the deploy, the bridge says whether the client needs changes. If it does, it makes
   them on `final_night_7_unreal`, tests them against the backend's DEV build if the backend provides one, and
   replies "client ready" with the commit hash, or "hold".
3. **The backend deploys** STABLE and verifies through the public URL.
4. **Both builders build the same client commit:**
   - **Windows** (`#benito/where-were-we`, through the bridge): Win64 Development build, connect to STABLE,
     report login, room and one beat.
   - **Mac** (`#chris-biggun/finalnight-unreal`, through the backend): `git pull` (discarding local edits only when
     told), Mac Development build, the same connect check.
5. **Both report back.** The backend tags the release only when the server is verified and both builds connect.
   The tag note records the server tag, engine commit and client commit together.
6. **A failure anywhere** (build error, connect error, payload mismatch) is a BUG (Play 1) with verbatim logs. The
   loop doesn't close until it's fixed or the release is rolled back (STABLE goes back to the previous tag).

The bridge always talks to the backend for anything that affects the server contract. The Mac verifier talks only
to the backend. Nobody pushes to another team's repo without an instruction.

**Play 9: git, the same for everyone, in every repo.** Re-read this after every compaction; save it to memory.
1. **`main` is the trunk.** Default: work on `main`.
   - Before every commit: `git pull --rebase origin main`.
   - Commit, push, then report to the backend: `<repo> <hash> <message> <files changed>`.
2. **Commit messages:** a short abstract name in brackets that says what changed, lowercase, 2–5 words.
   - Examples: `[ lobby join and host wait ]`, `[ quoted server url ]`, `[ lonlat order fix ]`, `[ simulator api ]`.
   - One change per commit, one distinct message per commit. Never a reused catch-all message.
   - No attribution lines.
   - If Chris gives an exact message, use his verbatim.
3. **Branches only when the backend says so**, named `<owner>/<topic>`:
   - owner: `backend`, `bridge`, `windows` or `mac`
   - topic: kebab-case, the same abstract style as commit names
   - e.g. `mac/lonlat-order-fix`, `bridge/media-cards`
4. **The backend directs every merge into `main`:**
   - It merges with the commit message `[ merge <topic> ]` (or fast-forwards), verifies (build and tests), deletes the branch, and tells everyone the new `main` hash to pull.
   - Nobody else merges into `main` from a branch.
5. **Never:** force-push, rewrite `main`'s history, push tags (tags are the backend's: `playtest-N`), or commit secrets (keys and tokens live in env vars and git-ignored local files).
6. **Committers:**
   - `final_night_7`: the backend.
   - `final_night_7_unreal`: the bridge, the Windows builder and the Mac verifier may all commit on `main` under rules 1–2. The backend keeps the history straight and relays hashes.
   - `final_night_7_web`: frozen.
   - `theatre`: the theatre_worker session only.
7. **This playbook exists in both repos:** `final_night_7/docs/PLAYBOOK.md` (the source) and
   `final_night_7_unreal/docs/PLAYBOOK.md` (a copy). The backend updates both together in one commit each.

**Play 7: the unexpected.** Anything not covered here: ask the backend first, then act. When in doubt, don't push,
don't deploy, don't delete.

## 4. Release notes format
```
RELEASE playtest-N  (rules X.Y.Z, engine <commit>)  deploy at: <time>
new: ...
fixed: ...
payload changes: <field: before → after>   (none = "none")
client action needed: ...                  (none = "none")
```

## 5. What the bridge can use (all on STABLE)
- Game: `wss://<URL>/ws?key=<KEY>`: protocol `docs/CLIENT_API.md`, reference client `docs/clients/fn7_client.mjs`.
- Game queries over HTTP: `GET /api/rooms/{code}/query/{name}?key=<KEY>` (public ones): `globe` (main poll),
  `events?since=`, `region`, `node`, `person`, `army`, `standings`, `powers`, `gods`, `piles_final`.
- Admin (time travel, fork, recordings): `Authorization: Bearer <ADMIN>`. See `docs/AGENT_GUIDE.md` §11.
- Simulator API `/sim/*` (balance batches): lands in an upcoming release, behind the admin token; usage in
  `docs/AGENT_GUIDE.md` §12 (check: `GET /sim/settings` answers 401/200 instead of 404 once it is live).
- Images: `docs/MEDIA.md` (placeholders until Chris sets a budget).

## 6. Rhythm
- The bridge sends a short status when it finishes a client milestone, or when Benito finishes a playtest (what felt
  wrong, settings used, match ids).
- The backend sends release notes and answers requests.
- Everything is recorded on the server. Quote match ids instead of describing; the backend can replay any match exactly.
- Hard deadline: the packaged Unreal build is due **Nov 15 2026, 8:59 PM EST**. Releases slow down as it approaches:
  after **Nov 15, 12:00 PM EST**, only blocking fixes.

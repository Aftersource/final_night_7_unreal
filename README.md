# Final Night 7: Unreal client

The Unreal Engine 5.8 client for **Final Night 7**, built on **The Theatre**. The server, game rules and docs are in
`Aftersource/final_night_7` (start with `docs/PLAYBOOK.md`, `docs/AGENT_GUIDE.md` and `docs/CLIENT_API.md`).

- Project: `FinalNight.uproject` (UE 5.8.4, Win64). The UI is Slate C++; networking is `UFNNetClient` (Theatre
  WebSocket protocol) plus HTTP game queries.
- Set the server URL and access key in a user-editable config, never in code. No LLM key ever ships in this client;
  all LLM work runs on the server.
- Binary assets go through Git LFS (`git lfs install` once per machine).

## Get it on a new machine
```sh
git lfs install
git clone https://github.com/Aftersource/final_night_7_unreal.git
```
Then open `FinalNight.uproject` with UE 5.8.4. It rebuilds `Binaries/` and `Intermediate/`, which aren't in git.

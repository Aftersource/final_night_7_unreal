#!/bin/bash
# Make a static, heroically posed 3D statue of a demigod or a god from one full-body image, CACHED by its slug:
# a slug that already has a statue is never generated again.
#
#   tools/meshy/make_statue.sh <kind: demigod|god> <slug> <heroic.png> "<display name>"
#
# COSTS MESHY CREDITS (about 30 for a textured model; docs/MESHY.md). Run it only with Chris's or Benito's go.
# Output: production_assets/meshy/figures/<slug>/<slug>_statue.glb + .fbx + preview.png, and a line in
# production_assets/meshy/figures/figures.json (the cache index the game reads: name -> files).
# Needs meshy-cli 0.4.0 and the key in __production_steps/meshy.env (git-ignored). Run from the repo root.
set -e
KIND=$1; SLUG=$2; IMG=$3; NAME=$4
[ -f "$IMG" ] && [ -n "$NAME" ] || { echo "usage: $0 <demigod|god> <slug> <heroic.png> \"<display name>\""; exit 1; }
W=production_assets/meshy; OUT=$W/figures/$SLUG; IDX=$W/figures/figures.json; mkdir -p "$OUT"
if [ -f "$OUT/${SLUG}_statue.glb" ]; then echo "cached: $OUT/${SLUG}_statue.glb (nothing spent)"; exit 0; fi
set -a; . __production_steps/meshy.env; set +a
F="--output-schema v1 --format json --no-update-check"
T=$(meshy image-to-3d create --image-url "$IMG" --model-type standard --should-texture true --enable-pbr true --texture-resolution 2k --target-formats glb,fbx --async $F 2>/dev/null | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['result']['submission']['task_id'] if d['ok'] else 'ERR '+json.dumps(d['error']))")
echo "statue task $T"; echo "$(date -u +%FT%TZ) statue $KIND $SLUG image-to-3d $T" >> __production_steps/meshy_jobs.log
case "$T" in ERR*) exit 1;; esac
P=$(meshy project init --root "$W" --name "statue_$SLUG" --task-id $T --task-type image-to-3d --workspace "$W" $F 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['project_dir'])")
C=$(meshy image-to-3d wait $T --timeout 900 --project "$P" --stage model --workspace "$W" $F 2>/dev/null | python3 -c "import sys,json; d=json.load(sys.stdin); t=d['result'].get('task',{}); print(t.get('status'), t.get('consumed_credits'))")
echo "result: $C"
meshy download --task-json "$P/task_$T.json" --asset thumbnail.primary --output "$OUT/preview.png" --workspace "$W" $F >/dev/null 2>&1 || true
meshy download --task-json "$P/task_$T.json" --asset model.glb --output "$OUT/${SLUG}_statue.glb" --workspace "$W" $F >/dev/null 2>&1 && echo "saved $OUT/${SLUG}_statue.glb"
meshy download --task-json "$P/task_$T.json" --asset model.fbx --output "$OUT/${SLUG}_statue.fbx" --workspace "$W" $F >/dev/null 2>&1 && echo "saved $OUT/${SLUG}_statue.fbx"
python3 - "$IDX" "$KIND" "$SLUG" "$NAME" "$T" "$C" <<'EOF'
import json, os, sys
idx, kind, slug, name, task, res = sys.argv[1:7]
d = json.load(open(idx)) if os.path.exists(idx) else {"version": 1, "figures": {}}
d["figures"][slug] = {"kind": kind, "name": name, "statue_glb": f"{slug}/{slug}_statue.glb", "statue_fbx": f"{slug}/{slug}_statue.fbx",
                      "preview": f"{slug}/preview.png", "source_image": f"{slug}/heroic.png", "meshy_task": task, "result": res}
json.dump(d, open(idx, "w"), indent=1, sort_keys=True)
EOF
meshy balance $F 2>/dev/null | python3 -c "import sys,json; print('balance', json.load(sys.stdin)['result']['balance'])"

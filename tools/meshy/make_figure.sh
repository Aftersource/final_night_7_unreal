#!/bin/bash
# Make a rigged 3D figure of a demigod from a full-body A-pose image, on the SAME skeleton as the duel mannequin
# (Meshy's ue5_mannequin template), so the mannequin's 35 clips play on it with no extra animation credits.
#
#   tools/meshy/make_figure.sh <slug> <apose.png>
#
# COSTS MESHY CREDITS: about 30 for the textured model + 5 for the rig (docs/MESHY.md). Run it only with Chris's or
# Benito's explicit go, and log the job in __production_steps/MESHY.md.
# Needs: meshy-cli 0.4.0 (npm install -g meshy-cli@0.4.0) and the key in __production_steps/meshy.env (git-ignored).
# Run from the repo root. The A-pose image: the whole body, arms away from the body, a plain background, no base,
# no props (see production_assets/meshy/figures/chris_left_hand/apose.png for one made from a game portrait).
set -e
SLUG=$1; IMG=$2; [ -f "$IMG" ] || { echo "usage: $0 <slug> <apose.png>"; exit 1; }
set -a; . __production_steps/meshy.env; set +a
W=production_assets/meshy; OUT=$W/figures/$SLUG; mkdir -p "$OUT"
F="--output-schema v1 --format json --no-update-check"
id(){ python3 -c "import sys,json; d=json.load(sys.stdin); print(d['result']['submission']['task_id'] if d['ok'] else 'ERR '+json.dumps(d['error']))"; }
st(){ python3 -c "import sys,json; d=json.load(sys.stdin); t=d['result'].get('task',{}); print(d['ok'], t.get('status'), 'credits', t.get('consumed_credits'), json.dumps(d.get('error'))[:200])"; }

T=$(meshy image-to-3d create --image-url "$IMG" --model-type standard --pose-mode a-pose --should-texture true --enable-pbr true --texture-resolution 2k --target-formats glb,fbx --async $F 2>/dev/null | id)
echo "model task $T"; echo "$(date -u +%FT%TZ) figure $SLUG image-to-3d $T" >> __production_steps/meshy_jobs.log
P=$(meshy project init --root "$W" --name "figure_$SLUG" --task-id $T --task-type image-to-3d --workspace "$W" $F 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['project_dir'])")
meshy image-to-3d wait $T --timeout 900 --project "$P" --stage model --workspace "$W" $F 2>/dev/null | st
meshy download --task-json "$P/task_$T.json" --asset thumbnail.primary --output "$OUT/preview.png" --workspace "$W" $F >/dev/null 2>&1 || true

R=$(meshy rigging create --input-task-id $T --height-meters 1.7 --data '{"skeleton_template":"ue5_mannequin"}' --async --project "$P" --stage rigging --workspace "$W" $F 2>/dev/null | id)
echo "rig task $R"; echo "$(date -u +%FT%TZ) figure $SLUG rigging $R" >> __production_steps/meshy_jobs.log
meshy rigging wait $R --timeout 900 --project "$P" --stage rigging --workspace "$W" $F 2>/dev/null | st
meshy download --task-json "$P/task_$R.json" --asset result.rigged_character_glb_url --output "$OUT/${SLUG}_rigged.glb" --workspace "$W" $F >/dev/null 2>&1 && echo "saved $OUT/${SLUG}_rigged.glb"
meshy download --task-json "$P/task_$R.json" --asset result.rigged_character_fbx_url --output "$OUT/${SLUG}_rigged.fbx" --workspace "$W" $F >/dev/null 2>&1 && echo "saved $OUT/${SLUG}_rigged.fbx"
meshy balance $F 2>/dev/null | python3 -c "import sys,json; print('balance', json.load(sys.stdin)['result']['balance'])"

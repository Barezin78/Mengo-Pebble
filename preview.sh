#!/bin/bash
# Preview a style on the emulator with mock data (does not touch the real build).
# usage: preview.sh <platform> <style 0|1|2> <mode 0 none|1 next|2 live|3 final> <out.png> [extra -D defines...]
set -e
PLAT=$1; STYLE=$2; MODE=$3; OUT=$(realpath -m "$4"); shift 4
SRC=$(cd "$(dirname "$0")" && pwd)
TMP=/tmp/pf-preview
rm -rf $TMP && mkdir -p $TMP && cp -r $SRC/package.json $SRC/package-lock.json $SRC/wscript $SRC/resources $SRC/src $TMP/
# previews use mock data, so drop the phone JS (keeps the emulator's pypkjs from crashing on XHR)
rm -rf $TMP/src/pkjs
python3 - "$TMP/package.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1])); p["dependencies"]={}; p["pebble"]["enableMultiJS"]=False
json.dump(p,open(sys.argv[1],"w"),indent=2,ensure_ascii=False)
PY
# kick-off for "next": next Sunday 16:00 local
KO=$(python3 -c "
import datetime as d,time
n=d.datetime.now(); t=n+d.timedelta(days=(6-n.weekday())%7 or 7)
t=t.replace(hour=16,minute=0,second=0,microsecond=0); print(int(t.timestamp()-time.time()))")
[ "$MODE" = "2" ] && KO=-4000
[ "$MODE" = "3" ] && KO=-7000
DEFS="#define PREVIEW_MOCK 1\n#define PREVIEW_MODE $MODE\n#define PREVIEW_DAY 1\n#define PREVIEW_KO_OFFSET ($KO)\n#define PREVIEW_STYLE $STYLE\n"
for d in "$@"; do DEFS="$DEFS#define $d\n"; done
printf "$DEFS" | cat - $SRC/src/c/main.c > $TMP/src/c/main.c
cd $TMP && pebble build > /tmp/pf-preview-build.log 2>&1 || { grep -E "error" /tmp/pf-preview-build.log; exit 1; }
pebble install --emulator $PLAT 2>&1 | grep -q "install succeeded" || { sleep 3; pebble install --emulator $PLAT; }
pebble emu-battery --emulator $PLAT --percent 80 >/dev/null 2>&1 || true
sleep 6
pebble screenshot --emulator $PLAT --no-open "$OUT" > /dev/null
echo "saved $OUT"

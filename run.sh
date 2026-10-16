#!/usr/bin/env bash
# Roda o jogo por N segundos (padrão 30) e resume onde ele está.
cd "$(dirname "$0")"; rm -f ps2_log.txt
export PS2X_BINDS="$PWD/config/binds.txt"
export PS2X_CD_IMAGE="$PWD/Downhill Domination (USA)/Downhill Domination (USA).iso"
timeout "${1:-30}" stdbuf -oL -eL PS2Recomp/out/${BUILD:-dev}/ps2xRuntime/ps2EntryRunner game/SCUS_971.77 > run.log 2>&1; echo "exit=$?"
sed -n '/Starting execution/,$p' run.log | cut -c1-200 | sed -n '4,25p'
awk '{$1="";print}' ps2_log.txt | cut -c1-80 | sort | uniq -c | sort -rn | head -5

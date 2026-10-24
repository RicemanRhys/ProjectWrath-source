cd $(dirname $(readlink -f $0))
until grep -q "fp106 done" fp106.out 2>/dev/null; do sleep 20; done
for r in "wC C" "wC0 C RECOMP_D3D_BRIDGE_OVERLAY_BIAS=0" "wF F"; do bash wall106.sh $r; done
for r in "wdC C" "wdC0 C RECOMP_D3D_BRIDGE_OVERLAY_BIAS=0" "wdF F"; do FR=display bash wall106.sh $r; done
echo wall done

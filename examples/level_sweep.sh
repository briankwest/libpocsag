#!/bin/bash
# level_sweep.sh — Interactive POCSAG TX level sweep
# Transmits a page at each level and asks if the pager decoded it.
# Usage: ./level_sweep.sh [options]
#   -a  pager address/capcode (default 1234567)
#   -b  baud rate: 512, 1200, 2400 (default 1200)
#   -P  PTT device (default /dev/hidraw1)
#   -D  audio device (default hw:1,0)
#   -d  TX delay ms (default 1500)
#   -m  message text (default "sweep test")

LIBPATH="/home/brian/libpocsag/src/.libs"
BINDIR="/home/brian/libpocsag/examples/.libs"

ADDR=1234567
BAUD=1200
HIDRAW="/dev/hidraw1"
AUDIO="hw:1,0"
DELAY=1500
MSG="sweep test"

while getopts "a:b:P:D:d:m:" opt; do
    case $opt in
        a) ADDR="$OPTARG" ;;
        b) BAUD="$OPTARG" ;;
        P) HIDRAW="$OPTARG" ;;
        D) AUDIO="$OPTARG" ;;
        d) DELAY="$OPTARG" ;;
        m) MSG="$OPTARG" ;;
    esac
done

TX() {
    local level="$1"
    sudo LD_LIBRARY_PATH="$LIBPATH" "$BINDIR/pocsag_tx" \
        -a "$ADDR" -m "${MSG} l${level}" -b "$BAUD" \
        -l "$level" -P "$HIDRAW" -D "$AUDIO" -d "$DELAY" 2>/dev/null
}

echo "========================================="
echo " POCSAG Level Sweep"
echo "========================================="
echo " Address: $ADDR"
echo " Baud:    $BAUD"
echo " PTT:     $HIDRAW"
echo " Audio:   $AUDIO"
echo " Delay:   ${DELAY}ms"
echo ""
echo " Commands at prompt:"
echo "   y/n    — decoded or not"
echo "   r      — repeat same level"
echo "   NUMBER — jump to that level (e.g. 0.07)"
echo "   b NUM  — change baud rate"
echo "   d NUM  — set TX delay"
echo "   q      — quit"
echo "========================================="
echo ""

LEVELS=(0.01 0.02 0.03 0.04 0.05 0.06 0.07 0.08 0.10 0.12 0.15 0.20 0.25 0.30 0.40 0.50)
RESULTS=()
IDX=0

while true; do
    if [ $IDX -ge ${#LEVELS[@]} ]; then
        echo ""
        echo "=== Sweep complete ==="
        break
    fi

    LVL="${LEVELS[$IDX]}"
    echo -n "TX level $LVL (${BAUD} baud) ... "
    TX "$LVL"
    echo "sent."

    while true; do
        read -p "  Decoded? [y/n/r/NUMBER/b NUM/d NUM/q]: " ans
        case "$ans" in
            y|Y)
                echo "  >>> $LVL DECODED <<<"
                RESULTS+=("$LVL@${BAUD}:YES")
                IDX=$((IDX + 1))
                break
                ;;
            n|N)
                RESULTS+=("$LVL@${BAUD}:NO")
                IDX=$((IDX + 1))
                break
                ;;
            r|R)
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            b\ *)
                BAUD="${ans#b }"
                echo "  Baud rate set to $BAUD"
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            d\ *)
                DELAY="${ans#d }"
                echo "  TX delay set to ${DELAY}ms"
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            q|Q)
                break 2
                ;;
            [0-9]*)
                LVL="$ans"
                LEVELS[$IDX]="$LVL"
                echo -n "  TX level $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            *)
                echo "  y/n/r/NUMBER/b NUM/d NUM/q"
                ;;
        esac
    done
done

echo ""
echo "========================================="
echo " Results"
echo "========================================="
for r in "${RESULTS[@]}"; do
    key="${r%%:*}"
    res="${r##*:}"
    if [ "$res" = "YES" ]; then
        printf "  %-14s  DECODED\n" "$key"
    else
        printf "  %-14s  no decode\n" "$key"
    fi
done
echo "========================================="

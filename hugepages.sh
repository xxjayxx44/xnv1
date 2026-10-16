#!/bin/sh
# language: sh, file: hugepages.sh
# Reserve 2 MB hugepages for the Nerva miner. Run once per boot (or persist
# in /etc/sysctl.d). Needs root.
#
# Kernel backing rules the miner cares about:
#   - MAP_HUGETLB is the top tier and only succeeds if pages are reserved.
#   - THP is best effort and the kernel can decline.
#   - malloc is the fallback and has the worst TLB behavior on the pad.
# The daemon's allocate_hugepage() tries them in that order and the miner's
# cn_page_tier_for_version() reports which one actually got picked.
#
# 512 pages * 2 MB = 1 GB, enough for one thread's pad+salt plus headroom.
# Multiply by thread count for multi-thread.

set -e

THREADS=${1:-$(nproc 2>/dev/null || echo 1)}
# Each hash thread works in ~1.25 MB (1 MB pad + 256 KB salt). Round up to
# 2 MB pages and add 20% headroom so the daemon's allocator has slack.
PAGES=$(( (THREADS * 2 * 12 / 10) + 16 ))

echo "threads=$THREADS  reserving $PAGES hugepages ($((PAGES * 2)) MB)"

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root: sudo $0 [threads]" >&2
    exit 1
fi

sysctl -w vm.nr_hugepages="$PAGES"

cat > /etc/sysctl.d/99-nerva-hugepages.conf <<EOF
vm.nr_hugepages = $PAGES
EOF

grep -E 'HugePages_Total|HugePages_Free|Hugepagesize' /proc/meminfo

echo
echo "If HugePages_Free is below HugePages_Total, another process is holding"
echo "pages. If HugePages_Total is 0, the kernel refused the reservation —"
echo "check dmesg for 'hugepage' errors, and check that the kernel command"
echo "line does not say transparent_hugepage=never."

#!/bin/sh
# Every gate of the repository, one version at a time (configure.py writes build.ninja for one version):
# the pre-1.3 versions relink exactly, GC_1_0 and GC_1_3 compare 2178/2198 and 2787/3684 functions, and 1.3, 1.3.2 and 2.7
# relink SHA-exact (1.3 with its fitting units from source, 1.3.2 and 2.7 from split objects).
cd "$(dirname "$0")/.." || exit 1
for v in GC_1_1 GC_1_1p1 GC_1_2_5 GC_1_2_5n GC_1_0 GC_1_3; do
    python configure.py --version $v > /dev/null 2>&1 && ninja > /dev/null 2>&1
    echo "$v: $(head -c 150 build/$v/ok 2>/dev/null | head -1)"
done
python configure.py --version GC_1_3 --mode link > /dev/null 2>&1 && ninja build/GC_1_3/ok > /dev/null 2>&1
echo "GC_1_3 link: $(head -c 150 build/GC_1_3/ok | head -1)"
python configure.py --version GC_1_3_2 --mode link --split-only > /dev/null 2>&1 && ninja build/GC_1_3_2/ok > /dev/null 2>&1
echo "GC_1_3_2 link: $(head -c 150 build/GC_1_3_2/ok | head -1)"
python configure.py --version GC_2_7 --mode link --split-only > /dev/null 2>&1 && ninja build/GC_2_7/ok > /dev/null 2>&1
echo "GC_2_7 link: $(head -c 150 build/GC_2_7/ok | head -1)"

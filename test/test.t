#!/usr/bin/env bash

BASH_TAP_ROOT=./bash-tap
. ${BASH_TAP_ROOT}/bash-tap-bootstrap

PATH=../bin:$PATH
PATH=../deps/hal:$PATH

plan tests 3

gzip -dc  hpp-20-2M/CHM13.fa.gz > CHM13.fa
gzip -dc  hpp-20-2M/hg38.fa.gz > hg38.fa

# validate the validator
minimap2 hpp-20-2M/CHM13.fa.gz hpp-20-2M/hg38.fa.gz -c -A 30 -B 10 > CHM13_hg38.paf
python ./verify_matches.py CHM13_hg38.paf CHM13.fa hg38.fa --min-identity 0.75
is $? 0 "validator accepts minimap paf with identiy 0.75"

rm -f CHM13_hg38.paf

# make the graph
minigraph -xggs -l10k hpp-20-2M/CHM13.fa.gz hpp-20-2M/HG003.fa.gz hpp-20-2M/HG004.fa.gz > hpp-20-2M.gfa
gfatools gfa2fa hpp-20-2M.gfa > hpp-20-2M.gfa.fa

# align CHM back to it
minigraph -xasm -t $(nproc) -K4g --inv=no -S --write-mz hpp-20-2M.gfa hpp-20-2M/CHM13.fa.gz > CHM13.gaf
mzgaf2paf CHM13.gaf > CHM13.paf
python ./verify_matches.py CHM13.paf CHM13.fa hpp-20-2M.gfa.fa
is $? 0 "paf checks out for very simple forward alignment"

rm -f  CHM13.gaf CHM13.paf

# align a new sequence (hg38) to it
minigraph -xasm -t $(nproc) -K4g --inv=no -S --write-mz hpp-20-2M.gfa hpp-20-2M/hg38.fa.gz > hg38.gaf
mzgaf2paf hg38.gaf > hg38.paf
python ./verify_matches.py hg38.paf hg38.fa hpp-20-2M.gfa.fa
is $? 0 "paf checks out for hg38 alignment"

rm -f  hg38.gaf hg38.paf

rm -f hpp-20-2M.gfa hpp-20-2M.gfa.fa CHM13.fa hg38.fa

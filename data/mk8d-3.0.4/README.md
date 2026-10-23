# Hot block list (MK8D 3.0.4)

`hot_blocks.txt`: names of the recompiled block functions (`blk_<module>_<16 hex digit guest offset>`) that ran during
our coverage runs (a replayed Grand Prix and scripted races on three cups incl. Time Trials and drift effects, all
without mods) plus the hot blocks of earlier CPU profiles. Addresses only: no game code or data. `tools/recomp/split_export.py
--hot=` compiles these with full optimisation and the rest (about 70% of the code, mostly never run) with `-O0`, which cuts the
compile of the generated code about 8x (see docs/PIPELINE.md).

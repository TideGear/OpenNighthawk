# DOSBox DBOPL

Copied from GOG's supplied DOSBox 0.74-2.1 source archive:
`src/hardware/dbopl.cpp` and `dbopl.h`, GPL-2.0-or-later (see COPYING).
Copyright 2002-2010 The DOSBox Team. Original SHA-256 hashes:

- cpp: `e538f5021c960aa180e43ec7910646b17b4948284723d8aa6c7dd873777e5141`
- h: `d0b98e9df2b35b1fe83af96fd4fdc45b392a53827f13259110384193c7c61e45`

Adaptations: remove the DOSBox-specific `Adlib::Handler` and mixer wrappers,
declare `InitTables`, and supply the core's fixed-width types and compiler
macros in a minimal `dosbox.h`. Chip algorithms and tables are unchanged.
The application-facing C wrapper lives in `src/host/dbopl_bridge.cpp`.

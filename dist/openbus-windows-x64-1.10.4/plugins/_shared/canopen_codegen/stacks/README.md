# CANopen stack integration (OD C/H from openbus)

Generated object-dictionary sources are meant to be dropped into a **firmware**
project that already uses one of the open-source CANopen stacks. openbus itself
does **not** vendor or compile those stacks.

## CANopenNode (V4)

- Upstream: https://github.com/CANopenNode/CANopenNode  
- Editor reference: https://github.com/CANopenNode/CANopenEditor  
- License: Apache-2.0 / other (see upstream)

### Workflow

1. In openbus **CANopen Suite → EDS → Codegen**, choose **CANopenNode V4**.
2. Save `OD.h` and `OD.c` into your application folder (next to `main.c`).
3. Add both files to your CMake/Makefile sources.
4. `#include "OD.h"` and map `OD_entryList` / `OD_RAM` into your CANopenNode
   init (see CANopenNode `OD.h`/`OD.c` examples under `example/`).
5. Generated `ODA_*` macros use the same names as CANopenNode V4; if you include
   the real stack headers first, remove the fallback `#ifndef ODA_SDO_R` block
   from `OD.h` or leave it (include-guarded).

### Notes

- Storage lives in `OD_RAM`; entry table is `OD_entryList[]`.
- This generator produces a **practical** V4-shaped dictionary suitable for
  bringing up SDO/PDO access. For full CANopenNode multi-OD / `CO_config_t`
  setups, refine with CANopenEditor after the first export.

## CanFestival

- Upstream (classic): https://canfestival.org / various forks (e.g. beremiz/canfestival)  
- Generator heritage: `objdictgen` (`Node.c` / `Node.h`, `indextable`, `CO_Data`)  
- License: LGPLv2+ for the stack; **generated OD C is typically not GPL-encumbered**
  (confirm with the fork you use).

### Workflow

1. Choose **CanFestival** in Codegen; set **Node name** (C identifier).
2. Save `<Node>.c` / `<Node>.h` into your CanFestival application.
3. Ensure the build includes CanFestival `include/` (`data.h`, `objdictdef.h`).
4. Link `<Node>.c` and pass `&<Node>_Data` to the stack init / timer loop.

### Notes

- Symbols follow objdictgen: `<Node>_objdict`, `<Node>_scanIndexOD`,
  `<Node>_Data = CANOPEN_NODE_DATA_INITIALIZER(<Node>)`.
- ARRAY/RECORD indexes without a typed sub-0 get a synthetic `UNS8` count
  subindex (common CanFestival pattern).

## License reminder

Only the **generated** `.c`/`.h` files are produced by openbus. Copying stack
sources into your product is governed by the upstream licenses above.

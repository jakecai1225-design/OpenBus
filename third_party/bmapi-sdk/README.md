# BUSMUST BMAPI SDK (headers + Windows x64 runtime)

Device Manager shows "BUSMUST USB-CAN(FD) Family" (example: USB\\VID_0810&PID_E122).
This is **not** ZLG / PEAK / candlelight — it uses the proprietary **BMAPI** stack
(`BMAPI64.dll`, python-can interface `bmcan`).

## Layout

- `include/` — `bm_usb_def.h`, `bmapi.h`, `osal.h` (from [busmust/bmapi-sdk](https://github.com/busmust/bmapi-sdk))
- `bin/win64/BMAPI64.dll` — runtime library (release v1.14.2.43+)
- `LICENSE` — vendor license (BUSMUST hardware only)

## Refresh

Download a release zip from GitHub Releases, then copy:

```
include/*.h  →  third_party/bmapi-sdk/include/
bin/win64/BMAPI64.dll  →  third_party/bmapi-sdk/bin/win64/
```

Do not commit the full multi-platform SDK zip (`_extract/`, `*.zip`).

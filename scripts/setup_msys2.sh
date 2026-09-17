#!/usr/bin/env bash
# openbus — install / verify MSYS2 build deps via pacman (default: UCRT64)
#
# Usage (prefer MSYS2 UCRT64 shell):
#   bash scripts/setup_msys2.sh
#
# Or from PowerShell:
#   /c/msys64/usr/bin/bash.exe -lc "cd /d/openbus/openbus_20260727/sin && bash scripts/setup_msys2.sh"
set -euo pipefail

ENV_NAME="${SIN_MSYS2_ENV:-ucrt64}"
case "${ENV_NAME}" in
  ucrt64)  PKG_PREFIX="mingw-w64-ucrt-x86_64" ;;
  mingw64) PKG_PREFIX="mingw-w64-x86_64" ;;
  clang64) PKG_PREFIX="mingw-w64-clang-x86_64" ;;
  *)
    echo "Unsupported SIN_MSYS2_ENV=${ENV_NAME} (use ucrt64 / mingw64 / clang64)" >&2
    exit 1
    ;;
esac

export PATH="/${ENV_NAME}/bin:/usr/bin:${PATH:-}"

PACKAGES=(
  "${PKG_PREFIX}-toolchain"
  "${PKG_PREFIX}-cmake"
  "${PKG_PREFIX}-ninja"
  "${PKG_PREFIX}-pkgconf"
  "${PKG_PREFIX}-zlib"
  "${PKG_PREFIX}-qt6-base"
  "${PKG_PREFIX}-qt6-svg"
  "${PKG_PREFIX}-qt6-tools"
  "${PKG_PREFIX}-qt6-translations"
  "${PKG_PREFIX}-python"
  "${PKG_PREFIX}-python-pyzmq"
  "${PKG_PREFIX}-python-pyqt6"
  "${PKG_PREFIX}-gdb"
  # Plugin host (ZMQ) + Candle (libusb)
  "${PKG_PREFIX}-zeromq"
  "${PKG_PREFIX}-cppzmq"
  "${PKG_PREFIX}-libusb"
)

echo "==> MSYS2 env: ${ENV_NAME}"
echo "==> package prefix: ${PKG_PREFIX}"
echo "==> pacman -S --needed ..."
pacman -S --needed --noconfirm "${PACKAGES[@]}"

echo
echo "==> Verify tools (must live under /${ENV_NAME}/bin):"
fail=0
for tool in g++ gcc cmake ninja qmake6 windeployqt6 pkg-config python python3; do
  loc="$(command -v "${tool}" 2>/dev/null || true)"
  if [[ -n "${loc}" && "${loc}" == */${ENV_NAME}/bin/* ]]; then
    echo "  OK  ${tool}: ${loc}"
  elif [[ -n "${loc}" ]]; then
    echo "  WARN ${tool}: ${loc}  (not under /${ENV_NAME}/bin; check PATH)" >&2
  else
    echo "  FAIL ${tool} not found" >&2
    fail=1
  fi
done

QT_CFG="/${ENV_NAME}/lib/cmake/Qt6/Qt6Config.cmake"
if [[ -f "${QT_CFG}" ]]; then
  echo "  OK  Qt6 CMake config: ${QT_CFG}"
else
  echo "  FAIL ${QT_CFG}" >&2
  fail=1
fi

if [[ "${fail}" -ne 0 ]]; then
  echo >&2
  echo "Dependency check failed. Run this script inside the ${ENV_NAME} shell." >&2
  exit 1
fi

echo
echo "Done. Build (do not use native Windows CMake / Qt installer):"
echo "  python scripts/build.py configure --clean"
echo "  python scripts/build.py build -j\$(nproc)"
echo "  python scripts/build.py deploy"
echo "  python scripts/build.py run"

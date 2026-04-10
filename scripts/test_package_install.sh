#!/usr/bin/env bash
# test_package_install.sh
# Automates PRTOS SDK generation, installation, and example testing.
# Covers two packaging workflows:
#   1) make distro-run  → self-extracting .run installer
#   2) make distro-tar  → .tar.bz2 archive with embedded prtos-installer
#
# The target architecture is auto-detected from --sdk-dir suffix:
#   prtos-sdk-x86     → x86
#   prtos-sdk-aarch64  → aarch64
#   prtos-sdk-riscv64  → riscv64
#   prtos-sdk-amd64    → amd64
#
# Usage:
#   bash scripts/test_package_install.sh --sdk-dir <dir> --toolchain-dir <dir>
#
# Defaults:
#   sdk-dir:       /home/${USER}/prtos-sdk-x86
#   toolchain-dir: /usr/bin/
set -euo pipefail

PROGNAME="$(basename "${0}")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SDK_INSTALL_DIR="/home/${USER}/prtos-sdk-x86"
TOOLCHAIN_DIR="/usr/bin/"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

function info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
function warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
function error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

while [[ $# -gt 0 ]]; do
    case "${1}" in
        --sdk-dir)       SDK_INSTALL_DIR="${2}"; shift 2 ;;
        --toolchain-dir) TOOLCHAIN_DIR="${2}"; shift 2 ;;
        -h|--help)
            echo "Usage: ${PROGNAME} [--sdk-dir <dir>] [--toolchain-dir <dir>]"
            exit 0 ;;
        *) error "Unknown option: ${1}" ;;
    esac
done

# ── Auto-detect architecture from --sdk-dir suffix ────────────────────────────
# e.g. /home/user/prtos-sdk-aarch64 → aarch64
_sdk_basename="$(basename "${SDK_INSTALL_DIR}")"
case "${_sdk_basename}" in
    *-x86)      ARCH="x86" ;;
    *-aarch64)  ARCH="aarch64" ;;
    *-riscv64)  ARCH="riscv64" ;;
    *-amd64)    ARCH="amd64" ;;
    *)          error "Cannot detect arch from sdk-dir '${SDK_INSTALL_DIR}'. Name must end with -x86, -aarch64, -riscv64, or -amd64." ;;
esac

cd "${REPO_ROOT}"
cp "prtos_config.${ARCH}" prtos_config
# shellcheck source=/dev/null
_VER=$(. "${REPO_ROOT}/version" && echo "${PRTOSVERSION}")
DISTRO_BASE="prtos_${ARCH}_${_VER}"           # e.g. prtos_x86_1.0.0
RUN_FILE="${REPO_ROOT}/${DISTRO_BASE}.run"
TAR_FILE="${REPO_ROOT}/${DISTRO_BASE}.tar.bz2"
SDK_INSTALL_DIR_TAR="${SDK_INSTALL_DIR}-tar"   # separate dir for tar workflow

info "=== PRTOS ${ARCH} SDK Build, Install & Test ==="
info "Repository:    ${REPO_ROOT}"
info "Toolchain:     ${TOOLCHAIN_DIR}"
info "SDK dir (.run):  ${SDK_INSTALL_DIR}"
info "SDK dir (.tar):  ${SDK_INSTALL_DIR_TAR}"
echo ""

# ── Step 1: Build ─────────────────────────────────────────────────────────────
info "Step 1/7: Building PRTOS for ${ARCH}..."
make distclean
cp "prtos_config.${ARCH}" prtos_config
make defconfig
make
info "Build complete."
echo ""

# ══════════════════════════════════════════════════════════════════════════════
# Workflow A — self-extracting .run package
# ══════════════════════════════════════════════════════════════════════════════

# Step 2A: Create .run package
info "Step 2/7: Creating .run package (make distro-run)..."
make distro-run
if [[ ! -f "${RUN_FILE}" ]]; then
    error ".run package not created: ${RUN_FILE}"
fi
info "Package created: ${RUN_FILE}"
echo ""

# Step 3A: Install via .run
info "Step 3/7: Installing SDK from .run to ${SDK_INSTALL_DIR}..."
if [[ -d "${SDK_INSTALL_DIR}" ]]; then
    warn "Removing existing SDK at ${SDK_INSTALL_DIR}"
    rm -rf "${SDK_INSTALL_DIR}"
fi
# Feed answers: continue=y, install_dir, toolchain_dir, confirm=y
printf "y\n%s\n%s\ny\n" "${SDK_INSTALL_DIR}" "${TOOLCHAIN_DIR}" \
    | bash "${RUN_FILE}"
if [[ ! -d "${SDK_INSTALL_DIR}" ]]; then
    error "SDK installation failed: '${SDK_INSTALL_DIR}' not found"
fi
info "SDK installed at ${SDK_INSTALL_DIR}"
echo ""

# Step 4A: Run tests from .run-installed SDK
info "Step 4/7: Running ${ARCH} SDK tests (from .run install)..."
cp "${SCRIPT_DIR}/run_bail_test.sh" "${SDK_INSTALL_DIR}/run_bail_test.sh"
cd "${SDK_INSTALL_DIR}"
bash run_bail_test.sh --arch "${ARCH}" check-all
info ".run workflow tests PASSED"
echo ""

# ══════════════════════════════════════════════════════════════════════════════
# Workflow B — .tar.bz2 archive with embedded prtos-installer
# ══════════════════════════════════════════════════════════════════════════════

cd "${REPO_ROOT}"

# Step 5B: Create .tar.bz2 package
info "Step 5/7: Creating .tar.bz2 package (make distro-tar)..."
make distro-tar
if [[ ! -f "${TAR_FILE}" ]]; then
    error ".tar.bz2 package not created: ${TAR_FILE}"
fi
info "Package created: ${TAR_FILE}"
echo ""

# Step 6B: Extract and install via prtos-installer
info "Step 6/7: Installing SDK from .tar.bz2 to ${SDK_INSTALL_DIR_TAR}..."
if [[ -d "${SDK_INSTALL_DIR_TAR}" ]]; then
    warn "Removing existing SDK at ${SDK_INSTALL_DIR_TAR}"
    rm -rf "${SDK_INSTALL_DIR_TAR}"
fi

TAR_EXTRACT_DIR="/tmp/${DISTRO_BASE}-extract-$$"
mkdir -p "${TAR_EXTRACT_DIR}"
tar xf "${TAR_FILE}" -C "${TAR_EXTRACT_DIR}"
INSTALLER="${TAR_EXTRACT_DIR}/${DISTRO_BASE}/prtos-installer"
if [[ ! -f "${INSTALLER}" ]]; then
    error "prtos-installer not found in extracted archive: ${INSTALLER}"
fi

# The installer uses relative paths (source prtos/prtos_config), so it must be
# run from within the extracted package directory.
# Feed answers: continue=y, install_dir, toolchain_dir, confirm=y
printf "y\n%s\n%s\ny\n" "${SDK_INSTALL_DIR_TAR}" "${TOOLCHAIN_DIR}" \
    | (cd "${TAR_EXTRACT_DIR}/${DISTRO_BASE}" && bash ./prtos-installer)

rm -rf "${TAR_EXTRACT_DIR}"

if [[ ! -d "${SDK_INSTALL_DIR_TAR}" ]]; then
    error "SDK installation failed: '${SDK_INSTALL_DIR_TAR}' not found"
fi
info "SDK installed at ${SDK_INSTALL_DIR_TAR}"
echo ""

# Step 7B: Run tests from .tar-installed SDK
info "Step 7/7: Running ${ARCH} SDK tests (from .tar.bz2 install)..."
cp "${SCRIPT_DIR}/run_bail_test.sh" "${SDK_INSTALL_DIR_TAR}/run_bail_test.sh"
cd "${SDK_INSTALL_DIR_TAR}"
bash run_bail_test.sh --arch "${ARCH}" check-all
info ".tar.bz2 workflow tests PASSED"
echo ""

info "=== All tests PASSED (both .run and .tar.bz2 workflows) ==="

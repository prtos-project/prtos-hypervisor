#!/usr/bin/env bash
# This script is used to run bail test cases in the bail-examples/directory.
set -o pipefail
unset LANG
unset LC_ALL
unset LC_COLLATE

PROGNAME="$(basename "${0}")"
ARCH=""
BUILDER=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Complete test case list: name, expected_verification_count, timeout_seconds[, arch_list]
# Format: "case_name:expected_count:timeout" or "case_name:expected_count:timeout:arch1,arch2,..."
# Cases without arch_list run on all architectures.
ALL_CASES=(
    "example.001:2:20"
    "example.002:1:8"
    "example.003:3:12"
    "example.004:1:15"
    "example.005:1:8"
    "example.006:1:30"
    "example.007:1:40"
    "example.008:2:15:x86,aarch64,riscv64,amd64"
    "example.009:2:15"
    "helloworld:1:15"
    "helloworld_smp:2:20:x86,aarch64,riscv64,amd64"
    "freertos_para_virt_aarch64:1:20:aarch64"
    "freertos_hw_virt_aarch64:0:30:aarch64"
    "freertos_para_virt_riscv:1:20:riscv64"
    "freertos_hw_virt_riscv:0:30:riscv64"
    "freertos_para_virt_amd64:1:20:amd64"
    "freertos_hw_virt_amd64:0:30:amd64"
    "linux_aarch64:0:180:aarch64"
    "linux_4vcpu_1partion_aarch64:0:360:aarch64"
    "linux_4vcpu_1partion_riscv64:0:360:riscv64"
    "linux_4vcpu_1partion_amd64:0:360:amd64"
    "mix_os_demo_aarch64:0:420:aarch64"
    "mix_os_demo_riscv64:0:420:riscv64"
    "mix_os_demo_amd64:0:420:amd64"
)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

function usage() {
cat <<EOF
Usage:
${PROGNAME} [options] <command>

Options:
  -h|--help              Display this help and exit.
  --arch <x86|aarch64|riscv64|amd64>   Target architecture (default: x86).

Commands:
  check-<case>           Check a specific test case.
                         Examples: check-helloworld, check-example.001
  check-all              Check all test cases (auto-discovered from bail-examples/).
  list-cases             List all available test cases.

Examples:
  ${PROGNAME} check-all                     # Run all x86 tests
  ${PROGNAME} --arch aarch64 check-all      # Run all AArch64 tests
  ${PROGNAME} --arch aarch64 check-example.001  # Run single AArch64 test
  ${PROGNAME} check-helloworld              # Run x86 helloworld test
  ${PROGNAME} list-cases                    # List all available test cases
EOF
}

if [[ $# == 0 ]]; then
    usage
    exit 0
fi

while [[ $# -gt 0 ]]; do
    case ${1} in
        -h|--help)
            usage
            exit 0
            ;;
        --arch)
            ARCH="${2}"
            shift 2
            ;;
        *)
            BUILDER="${1}"
            shift
            ;;
    esac
done

# Default architecture
if [[ -z "${ARCH}" ]]; then
    ARCH="x86"
fi

# Validate architecture
if [[ "${ARCH}" != "x86" && "${ARCH}" != "aarch64" && "${ARCH}" != "riscv64" && "${ARCH}" != "amd64" ]]; then
    echo "Error: unsupported architecture '${ARCH}'. Use 'x86', 'aarch64', 'riscv64', or 'amd64'."
    exit 1
fi

# Set paths relative to script location
MONOREPO_ROOT="${SCRIPT_DIR}"
MAKE="make"

if [[ "${ARCH}" == "x86" ]]; then
    QEMU="qemu-system-i386"
    MAKE_RUN_TARGET="run.x86.nographic"
    CONFIG_FILE="prtos_config.x86"
elif [[ "${ARCH}" == "riscv64" ]]; then
    QEMU="qemu-system-riscv64"
    MAKE_RUN_TARGET="run.riscv64"
    CONFIG_FILE="prtos_config.riscv64"
elif [[ "${ARCH}" == "amd64" ]]; then
    QEMU="qemu-system-x86_64"
    MAKE_RUN_TARGET="run.amd64.nographic"
    CONFIG_FILE="prtos_config.amd64"
else
    QEMU="qemu-system-aarch64"
    MAKE_RUN_TARGET="run.aarch64"
    CONFIG_FILE="prtos_config.aarch64"
fi

# Test report tracking
declare -a REPORT_CASES=()
declare -a REPORT_RESULTS=()
TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0

function clean() {
    cd "${MONOREPO_ROOT}"
    ${MAKE} distclean 2>/dev/null
}

function build_prtos() {
    cd "${MONOREPO_ROOT}"
    cp "${CONFIG_FILE}" prtos_config
    ${MAKE} defconfig
    ${MAKE}
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}ERROR: Failed to build PRTOS for ${ARCH}${NC}"
        exit 1
    fi
}

# In SDK context, build_prtos is not needed — each example builds itself.
# Uncomment the function call in check-all/check-* if using from source tree.

# Lookup test case config: sets CASE_EXPECT, CASE_TIMEOUT, CASE_ARCH
# Returns 0 if found, 1 if not found
function lookup_case() {
    local case_name="$1"
    CASE_EXPECT=""
    CASE_TIMEOUT=""
    CASE_ARCH=""

    for entry in "${ALL_CASES[@]}"; do
        local name="${entry%%:*}"
        local rest="${entry#*:}"
        local expect="${rest%%:*}"
        rest="${rest#*:}"
        local timeout="${rest%%:*}"
        local arch=""
        if [[ "${rest}" == *:* ]]; then
            arch="${rest#*:}"
        fi
        if [[ "${name}" == "${case_name}" ]]; then
            CASE_EXPECT="${expect}"
            CASE_TIMEOUT="${timeout}"
            CASE_ARCH="${arch}"
            return 0
        fi
    done
    return 1
}

# Check if the current ARCH is in a comma-separated list of archs
# Returns 0 if ARCH is in the list (or list is empty meaning all archs)
function arch_matches() {
    local arch_list="$1"
    if [[ -z "${arch_list}" ]]; then
        return 0  # empty list means all archs
    fi
    local IFS=','
    for a in ${arch_list}; do
        if [[ "${a}" == "${ARCH}" ]]; then
            return 0
        fi
    done
    return 1
}

# Run a single test case
# Arguments: case_name
# Returns: 0 on PASS, 1 on FAIL
function run_test() {
    local case_name="$1"

    lookup_case "${case_name}"

    local test_dir="${MONOREPO_ROOT}/bail-examples/${case_name}"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    local output_file="${test_dir}/${case_name}.output"
    rm -f "${output_file}"

    echo "+++ Checking examples/${case_name} [${ARCH}]"

    cd "${test_dir}"
    make ${MAKE_RUN_TARGET} > "${output_file}" 2>&1 &
    local qemu_pid=$!

    sleep ${CASE_TIMEOUT}

    # Kill QEMU - try specific PID's children first, then fallback to killall
    killall -9 "${QEMU}" 2>/dev/null
    wait ${qemu_pid} 2>/dev/null

    local halted_num
    halted_num=$(tr -d '\r' < "${output_file}" 2>/dev/null | grep -c "Verification Passed$") || true
    halted_num="${halted_num:-0}"

    if [[ ${halted_num} -eq ${CASE_EXPECT} ]]; then
        echo -e "${GREEN}Check ${case_name} PASS${NC}"
        return 0
    else
        echo -e "${RED}Check ${case_name} FAILED${NC} (expected ${CASE_EXPECT} 'Verification Passed', got ${halted_num})"
        return 1
    fi
}

# Record result into report
function record_result() {
    local case_name="$1"
    local result="$2"  # PASS, FAIL, SKIP
    REPORT_CASES+=("${case_name}")
    REPORT_RESULTS+=("${result}")
    case "${result}" in
        PASS) ((TOTAL_PASS++)) ;;
        FAIL) ((TOTAL_FAIL++)) ;;
        SKIP) ((TOTAL_SKIP++)) ;;
    esac
}

# Print test report
function print_report() {
    local total=${#REPORT_CASES[@]}
    echo ""
    echo "======================================"
    echo "  Test Report [${ARCH}]"
    echo "======================================"
    for i in "${!REPORT_CASES[@]}"; do
        local case_name="${REPORT_CASES[$i]}"
        local result="${REPORT_RESULTS[$i]}"
        case "${result}" in
            PASS) printf "  %-20s ${GREEN}%s${NC}\n" "${case_name}" "${result}" ;;
            FAIL) printf "  %-20s ${RED}%s${NC}\n" "${case_name}" "${result}" ;;
            SKIP) printf "  %-20s ${YELLOW}%s${NC}\n" "${case_name}" "${result}" ;;
        esac
    done
    echo "--------------------------------------"
    echo -e "  Total: ${total}  ${GREEN}Pass: ${TOTAL_PASS}${NC}  ${RED}Fail: ${TOTAL_FAIL}${NC}  ${YELLOW}Skip: ${TOTAL_SKIP}${NC}"
    echo "======================================"
}

# Map check-NNN to case name
function builder_to_case() {
    local builder="$1"
    case "${builder}" in
        check-*) echo "${builder#check-}" ;;
        *)       echo "" ;;
    esac
}

# ---- Main ----

case "${BUILDER}" in
list-cases)
    echo "Available test cases:"
    echo "----------------------------------------"
    for entry in "${ALL_CASES[@]}"; do
        _name="${entry%%:*}"
        _rest="${entry#*:}"; _rest="${_rest#*:}"; _arch=""
        if [[ "${_rest}" == *:* ]]; then _arch="${_rest#*:}"; fi
        if [[ -n "${_arch}" ]]; then
            echo "  ${_name} [${_arch} only]"
        else
            echo "  ${_name}"
        fi
    done
    echo ""
    echo "Total: ${#ALL_CASES[@]} test cases"
;;

check-all)
    clean
    echo "+++ Building PRTOS for ${ARCH}"
    # build_prtos

    for entry in "${ALL_CASES[@]}"; do
        _case_name="${entry%%:*}"
        lookup_case "${_case_name}"
        # Skip if this case doesn't apply to the current architecture
        if ! arch_matches "${CASE_ARCH}"; then
            record_result "${_case_name}" "SKIP"
            continue
        fi
        # Skip if test directory doesn't exist in bail-examples
        if [[ ! -d "${MONOREPO_ROOT}/bail-examples/${_case_name}" ]]; then
            record_result "${_case_name}" "SKIP"
            continue
        fi
        if run_test "${_case_name}"; then
            record_result "${_case_name}" "PASS"
        else
            record_result "${_case_name}" "FAIL"
        fi
    done

    print_report
    clean

    if [[ ${TOTAL_FAIL} -gt 0 ]]; then
        exit 1
    fi
;;

check-*)
    case_name="$(builder_to_case "${BUILDER}")"
    if [[ -z "${case_name}" ]]; then
        echo "Error: '${BUILDER}' is not a valid test command"
        usage
        exit 1
    fi

    clean
    echo "+++ Building PRTOS for ${ARCH}"
    # build_prtos

    if run_test "${case_name}"; then
        record_result "${case_name}" "PASS"
    else
        record_result "${case_name}" "FAIL"
    fi

    print_report
    clean

    if [[ ${TOTAL_FAIL} -gt 0 ]]; then
        exit 1
    fi
;;

*)
    echo "Error: '${BUILDER}' is not a known command"
    usage
    exit 1
;;
esac

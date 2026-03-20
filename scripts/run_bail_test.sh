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

# Test case definitions: name, expected_verification_count, timeout_seconds
# Format: "case_name:expected_count:timeout"
# For cases not listed here, defaults will be used
CASE_CONFIGS=(
    "example.001:2:20"
    "example.002:1:8"
    "example.003:3:12"
    "example.004:1:15"
    "example.005:1:8"
    "example.006:1:20"
    "example.007:1:40"
    "example.008:2:15"
    "example.009:2:15"
    "helloworld:1:15"
    "helloworld_smp:2:15"
)

# Default values for test cases not in CASE_CONFIGS
DEFAULT_EXPECT=1
DEFAULT_TIMEOUT=15

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
  --arch <x86|aarch64>   Target architecture (default: x86).

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
if [[ "${ARCH}" != "x86" && "${ARCH}" != "aarch64" ]]; then
    echo "Error: unsupported architecture '${ARCH}'. Use 'x86' or 'aarch64'."
    exit 1
fi

# Set paths relative to script location
MONOREPO_ROOT="${SCRIPT_DIR}"
MAKE="make"

if [[ "${ARCH}" == "x86" ]]; then
    QEMU="qemu-system-i386"
    MAKE_RUN_TARGET="run.x86.nographic"
    CONFIG_FILE="prtos_config.x86"
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

# function build_prtos() {
#     cd "${MONOREPO_ROOT}"
#     cp "${CONFIG_FILE}" prtos_config
#     ${MAKE} defconfig
#     ${MAKE}
#     if [[ $? -ne 0 ]]; then
#         echo -e "${RED}ERROR: Failed to build PRTOS for ${ARCH}${NC}"
#         exit 1
#     fi
# }

# Lookup test case config: sets CASE_EXPECT and CASE_TIMEOUT
# Returns 0 always (uses defaults if case not found)
function lookup_case() {
    local case_name="$1"
    CASE_EXPECT="${DEFAULT_EXPECT}"
    CASE_TIMEOUT="${DEFAULT_TIMEOUT}"
    
    for entry in "${CASE_CONFIGS[@]}"; do
        local name="${entry%%:*}"
        local rest="${entry#*:}"
        local expect="${rest%%:*}"
        local timeout="${rest#*:}"
        if [[ "${name}" == "${case_name}" ]]; then
            CASE_EXPECT="${expect}"
            CASE_TIMEOUT="${timeout}"
            return 0
        fi
    done
    # Case not found in configs, use defaults
    echo -e "${YELLOW}Note: Using default config for ${case_name} (expect=${DEFAULT_EXPECT}, timeout=${DEFAULT_TIMEOUT})${NC}"
    return 0
}

# Discover all test cases from bail-examples directory
function discover_all_cases() {
    local test_dir="${MONOREPO_ROOT}/bail-examples"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Error: Test directory not found: ${test_dir}${NC}"
        exit 1
    fi
    
    local cases=()
    for dir in "${test_dir}"/*/; do
        if [[ -d "${dir}" ]]; then
            local case_name="$(basename "${dir}")"
            # Skip if not a valid test case (must have Makefile)
            if [[ -f "${dir}/Makefile" ]]; then
                cases+=("${case_name}")
            fi
        fi
    done
    
    # Sort the cases
    IFS=$'\n' sorted_cases=($(sort <<<"${cases[*]}")); unset IFS
    echo "${sorted_cases[@]}"
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
    halted_num=$(grep -c "Verification Passed$" "${output_file}" 2>/dev/null || echo 0)

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
    echo "Available test cases in bail-examples/:"
    echo "----------------------------------------"
    all_cases=($(discover_all_cases))
    for case_name in "${all_cases[@]}"; do
        echo "  ${case_name}"
    done
    echo ""
    echo "Total: ${#all_cases[@]} test cases"
;;

check-all)
    clean
    echo "+++ Building PRTOS for ${ARCH}"
    # build_prtos

    all_cases=($(discover_all_cases))
    for case_name in "${all_cases[@]}"; do
        if run_test "${case_name}"; then
            record_result "${case_name}" "PASS"
        else
            record_result "${case_name}" "FAIL"
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

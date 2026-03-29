#!/usr/bin/env bash

set -o pipefail
unset LANG
unset LC_ALL
unset LC_COLLATE

PROGNAME="$(basename "${0}")"
ARCH=""
BUILDER=""

# Test case definitions: name, expected_verification_count, timeout_seconds
# Format: "case_name:expected_count:timeout[:arch_list]"
# arch_list: comma-separated architectures (empty = all archs)
ALL_CASES=(
    "example.001:2:20"
    "example.002:1:8"
    "example.003:3:12"
    "example.004:1:15"
    "example.005:1:8"
    "example.006:1:20"
    "example.007:1:40"
    "example.008:2:15:x86,aarch64,riscv64,amd64"
    "example.009:2:15"
    "helloworld:1:15"
    "helloworld_smp:2:20:x86,aarch64,riscv64,amd64"
    "freertos_para_virt:1:20:aarch64"
    "freertos_hw_virt:0:30:aarch64"
    "freertos_para_virt_riscv:1:20:riscv64"
    "freertos_hw_virt_riscv:0:30:riscv64"
    "linux:0:180:aarch64"
    "linux_4vcpu_1partion:0:360:aarch64"
    "linux_4vcpu_1partion_riscv64:0:360:riscv64"
    "mix_os_demo1:0:420:aarch64"
    "mix_os_demo_riscv64:0:420:riscv64"
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
  --arch <x86|aarch64>   Target architecture (default: x86).

Commands:
  check-<case>           Check a specific test case.
                         Available: helloworld, helloworld_smp,
                         example.001 ~ example.009,
                         freertos_para_virt (aarch64 only),
                         freertos_hw_virt (aarch64 only),
                         linux (aarch64 only),
                         linux_4vcpu_1partion (aarch64 only),
                         linux_4vcpu_1partion_riscv64 (riscv64 only),
                         mix_os_demo1 (aarch64 only),
                         mix_os_demo_riscv64 (riscv64 only)
  check-all              Check all test cases.

Examples:
  ${PROGNAME} check-all                     # Run all x86 tests
  ${PROGNAME} --arch aarch64 check-all      # Run all AArch64 tests
  ${PROGNAME} --arch aarch64 check-001      # Run single AArch64 test
  ${PROGNAME} check-helloworld              # Run x86 helloworld test
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

MONOREPO_ROOT="${MONOREPO_ROOT:="$(git rev-parse --show-toplevel)"}"
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

# Lookup test case config: sets CASE_EXPECT, CASE_TIMEOUT, CASE_ARCH
# Format: "name:expected_count:timeout[:arch]"
# If arch is specified, the test only runs on that architecture.
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

# Run the FreeRTOS hw_virt test case (aarch64 only)
# Builds native (unmodified) FreeRTOS under PRTOS hw-virt, checks for timer output.
# Returns: 0 on PASS, 1 on FAIL
function run_test_freertos_hw_virt() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/freertos_hw_virt"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/freertos_hw_virt [${ARCH}]"
    cd "${test_dir}"

    local output_file="${test_dir}/freertos_hw_virt.output"
    rm -f "${output_file}"

    make ${MAKE_RUN_TARGET} > "${output_file}" 2>&1 &
    local qemu_pid=$!

    sleep 30

    killall -9 "${QEMU}" 2>/dev/null
    wait ${qemu_pid} 2>/dev/null

    # Native FreeRTOS prints "Timer:XXXXX Stop" when each of 5 timers completes.
    local stop_count
    stop_count=$(grep -c "Stop$" "${output_file}" 2>/dev/null) || stop_count=0

    if [[ ${stop_count} -ge 5 ]]; then
        echo -e "${GREEN}Check freertos_hw_virt PASS${NC}"
        return 0
    else
        echo -e "${RED}Check freertos_hw_virt FAILED${NC} (expected >=5 'Stop' lines, got ${stop_count})"
        cat "${output_file}" 2>/dev/null || true
        return 1
    fi
}

# Run the FreeRTOS hw_virt_riscv test case (riscv64 only)
# Same as freertos_hw_virt but for RISC-V 64 platform.
function run_test_freertos_hw_virt_riscv() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/freertos_hw_virt_riscv"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/freertos_hw_virt_riscv [${ARCH}]"
    cd "${test_dir}"

    local output_file="${test_dir}/freertos_hw_virt_riscv.output"
    rm -f "${output_file}"

    make ${MAKE_RUN_TARGET} > "${output_file}" 2>&1 &
    local qemu_pid=$!

    sleep 30

    killall -9 "${QEMU}" 2>/dev/null
    wait ${qemu_pid} 2>/dev/null

    local stop_count
    stop_count=$(grep -c "Stop$" "${output_file}" 2>/dev/null) || stop_count=0

    if [[ ${stop_count} -ge 5 ]]; then
        echo -e "${GREEN}Check freertos_hw_virt_riscv PASS${NC}"
        return 0
    else
        echo -e "${RED}Check freertos_hw_virt_riscv FAILED${NC} (expected >=5 'Stop' lines, got ${stop_count})"
        cat "${output_file}" 2>/dev/null || true
        return 1
    fi
}

# Run the Linux test case (aarch64 only)
# Uses pexpect to boot, login (root/1234), and verify 2 vCPUs.
# Returns: 0 on PASS, 1 on FAIL
function run_test_linux() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/linux"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/linux [${ARCH}]"
    cd "${test_dir}"

    # Build the Linux partition
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Check linux FAILED${NC} (build error)"
        return 1
    fi

    # Create bootable image
    aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S \
        resident_sw resident_sw.bin
    mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 \
        -d resident_sw.bin resident_sw_image > /dev/null 2>&1
    mkdir -p u-boot
    cp ../../bin/u-boot.bin ./u-boot/

    # Run pexpect-based login test
    python3 -u << 'PYTEST' 2>&1
import pexpect, sys, time
child = pexpect.spawn(
    'qemu-system-aarch64 '
    '-machine virt,gic_version=3 '
    '-machine virtualization=true '
    '-cpu cortex-a72 -machine type=virt '
    '-m 4096 -smp 4 '
    '-bios ./u-boot/u-boot.bin '
    '-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on '
    '-nographic -no-reboot',
    timeout=200, encoding='utf-8', codec_errors='replace'
)
try:
    idx = child.expect(['buildroot login:', pexpect.TIMEOUT, pexpect.EOF], timeout=180)
    if idx != 0:
        print('LINUX_TEST_FAIL: login prompt not reached')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('root')
    idx = child.expect(['Password:', 'assword:', pexpect.TIMEOUT], timeout=30)
    if idx >= 2:
        print('LINUX_TEST_FAIL: no password prompt')
        child.close(force=True); sys.exit(1)
    time.sleep(1); child.sendline('1234')
    idx = child.expect(['#', 'Login incorrect', pexpect.TIMEOUT], timeout=30)
    if idx != 0:
        print('LINUX_TEST_FAIL: login failed')
        child.close(force=True); sys.exit(1)
    time.sleep(1); child.sendline('nproc')
    idx = child.expect(['2', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: nproc did not return 2')
        child.close(force=True); sys.exit(1)
    child.expect(['#', pexpect.TIMEOUT], timeout=5)
    child.sendline('which htop')
    idx = child.expect(['htop', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: htop not found')
        child.close(force=True); sys.exit(1)
    print('Verification Passed')
    child.close(force=True)
except Exception as e:
    print(f'LINUX_TEST_FAIL: {e}')
    try: child.close(force=True)
    except: pass
    sys.exit(1)
PYTEST

    local rc=$?
    if [[ ${rc} -eq 0 ]]; then
        echo -e "${GREEN}Check linux PASS${NC}"
        return 0
    else
        echo -e "${RED}Check linux FAILED${NC}"
        return 1
    fi
}

# Run the Linux 4-vCPU test case (aarch64 only)
# Uses pexpect to boot, login (root/1234), and verify 4 vCPUs.
# Returns: 0 on PASS, 1 on FAIL
function run_test_linux_4vcpu_1partion() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/linux_4vcpu_1partion"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/linux_4vcpu_1partion [${ARCH}]"
    cd "${test_dir}"

    # Build the Linux partition
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Check linux_4vcpu_1partion FAILED${NC} (build error)"
        return 1
    fi

    # Create bootable image
    aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S \
        resident_sw resident_sw.bin
    mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 \
        -d resident_sw.bin resident_sw_image > /dev/null 2>&1
    mkdir -p u-boot
    cp ../../bin/u-boot.bin ./u-boot/

    # Run pexpect-based login test
    python3 -u << 'PYTEST' 2>&1
import pexpect, sys, time
child = pexpect.spawn(
    'qemu-system-aarch64 '
    '-machine virt,gic_version=3 '
    '-machine virtualization=true '
    '-cpu cortex-a72 -machine type=virt '
    '-m 4096 -smp 4 '
    '-bios ./u-boot/u-boot.bin '
    '-device loader,file=./resident_sw_image,addr=0x40200000,force-raw=on '
    '-nographic -no-reboot',
    timeout=340, encoding='utf-8', codec_errors='replace'
)
try:
    idx = child.expect(['buildroot login:', pexpect.TIMEOUT, pexpect.EOF], timeout=320)
    if idx != 0:
        print('LINUX_TEST_FAIL: login prompt not reached')
        child.close(force=True); sys.exit(1)
    time.sleep(4); child.sendline('root')
    idx = child.expect(['Password:', 'assword:', pexpect.TIMEOUT], timeout=30)
    if idx >= 2:
        print('LINUX_TEST_FAIL: no password prompt')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('1234')
    idx = child.expect(['#', 'Login incorrect', pexpect.TIMEOUT], timeout=30)
    if idx != 0:
        print('LINUX_TEST_FAIL: login failed')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('nproc')
    idx = child.expect(['4', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: nproc did not return 4')
        child.close(force=True); sys.exit(1)
    child.expect(['#', pexpect.TIMEOUT], timeout=5)
    child.sendline('which htop')
    idx = child.expect(['htop', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: htop not found')
        child.close(force=True); sys.exit(1)
    print('Verification Passed')
    child.close(force=True)
except Exception as e:
    print(f'LINUX_TEST_FAIL: {e}')
    try: child.close(force=True)
    except: pass
    sys.exit(1)
PYTEST

    local rc=$?
    if [[ ${rc} -eq 0 ]]; then
        echo -e "${GREEN}Check linux_4vcpu_1partion PASS${NC}"
        return 0
    else
        echo -e "${RED}Check linux_4vcpu_1partion FAILED${NC}"
        return 1
    fi
}

# Run the Mixed-OS demo test (aarch64 only)
# FreeRTOS on vCPU0 + Linux on vCPU1-3.
# Verifies: RTOS prints status, Linux boots with 3 vCPUs, login works.
# Returns: 0 on PASS, 1 on FAIL
function run_test_mix_os_demo1() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/mix_os_demo1"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/mix_os_demo1 [${ARCH}]"
    cd "${test_dir}"

    # Build both partitions
    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Check mix_os_demo1 FAILED${NC} (build error)"
        return 1
    fi

    # Create bootable image
    aarch64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S \
        resident_sw resident_sw.bin
    mkimage -A arm64 -O linux -C none -a 0x40200000 -e 0x40200000 \
        -d resident_sw.bin resident_sw_image > /dev/null 2>&1

    # Run pexpect-based test (use -kernel boot, no u-boot needed for mixed-OS)
    python3 -u << 'PYTEST' 2>&1
import pexpect, sys, time
child = pexpect.spawn(
    'qemu-system-aarch64 '
    '-machine virt,gic_version=3 '
    '-machine virtualization=true '
    '-cpu cortex-a72 -machine type=virt '
    '-m 4096 -smp 4 '
    '-kernel ./resident_sw_image '
    '-nographic -no-reboot',
    timeout=400, encoding='utf-8', codec_errors='replace'
)
try:
    # Wait for Linux login prompt (RTOS output may be interleaved on same UART)
    idx = child.expect(['buildroot login:', pexpect.TIMEOUT, pexpect.EOF], timeout=380)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: login prompt not reached')
        child.close(force=True); sys.exit(1)
    # Check FreeRTOS output appeared during boot (interleaved with Linux boot)
    boot_output = child.before if child.before else ''
    rtos_found = 'RTOS' in boot_output
    if not rtos_found:
        print('MIX_OS_TEST_FAIL: no FreeRTOS output on serial')
        child.close(force=True); sys.exit(1)
    print('FreeRTOS serial output detected')
    # Retry login up to 3 times in case of interleaved RTOS output
    logged_in = False
    for attempt in range(3):
        time.sleep(6)
        child.sendline('root')
        idx = child.expect(['assword', 'buildroot login:', pexpect.TIMEOUT], timeout=60)
        if idx == 0:
            time.sleep(2); child.sendline('1234')
            idx2 = child.expect([r'[\$#] ', 'Login incorrect', pexpect.TIMEOUT], timeout=60)
            if idx2 == 0:
                logged_in = True
                break
            elif idx2 == 1:
                print('MIX_OS_TEST_FAIL: login incorrect')
                child.close(force=True); sys.exit(1)
        elif idx == 1:
            continue  # got login prompt again, retry
    if not logged_in:
        print('MIX_OS_TEST_FAIL: login failed after retries')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('nproc')
    idx = child.expect(['3', pexpect.TIMEOUT], timeout=15)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: nproc did not return 3')
        child.close(force=True); sys.exit(1)
    child.expect([r'[\$#] ', pexpect.TIMEOUT], timeout=10)
    child.sendline('cat /proc/cpuinfo | grep processor | wc -l')
    idx = child.expect(['3', pexpect.TIMEOUT], timeout=15)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: cpuinfo does not show 3 CPUs')
        child.close(force=True); sys.exit(1)
    print('Verification Passed')
    child.close(force=True)
except Exception as e:
    print(f'MIX_OS_TEST_FAIL: {e}')
    try: child.close(force=True)
    except: pass
    sys.exit(1)
PYTEST

    local rc=$?
    if [[ ${rc} -eq 0 ]]; then
        echo -e "${GREEN}Check mix_os_demo1 PASS${NC}"
        return 0
    else
        echo -e "${RED}Check mix_os_demo1 FAILED${NC}"
        return 1
    fi
}

# Run the Linux 4-vCPU test case (riscv64 only)
# Uses pexpect to boot, login (root/1234), and verify 4 vCPUs.
function run_test_linux_4vcpu_1partion_riscv64() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/linux_4vcpu_1partion_riscv64"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/linux_4vcpu_1partion_riscv64 [${ARCH}]"
    cd "${test_dir}"

    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Check linux_4vcpu_1partion_riscv64 FAILED${NC} (build error)"
        return 1
    fi

    riscv64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S \
        resident_sw resident_sw.bin

    python3 -u << 'PYTEST' 2>&1
import pexpect, sys, time
child = pexpect.spawn(
    'qemu-system-riscv64 '
    '-machine virt -cpu rv64 -smp 4 -m 1G '
    '-nographic -no-reboot '
    '-bios default -kernel resident_sw.bin '
    '-monitor none -serial stdio',
    timeout=340, encoding='utf-8', codec_errors='replace'
)
try:
    idx = child.expect(['buildroot login:', pexpect.TIMEOUT, pexpect.EOF], timeout=320)
    if idx != 0:
        print('LINUX_TEST_FAIL: login prompt not reached')
        child.close(force=True); sys.exit(1)
    time.sleep(4); child.sendline('root')
    idx = child.expect(['Password:', 'assword:', pexpect.TIMEOUT], timeout=30)
    if idx >= 2:
        print('LINUX_TEST_FAIL: no password prompt')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('1234')
    idx = child.expect(['#', 'Login incorrect', pexpect.TIMEOUT], timeout=30)
    if idx != 0:
        print('LINUX_TEST_FAIL: login failed')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('nproc')
    idx = child.expect(['4', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: nproc did not return 4')
        child.close(force=True); sys.exit(1)
    child.expect(['#', pexpect.TIMEOUT], timeout=5)
    child.sendline('which htop')
    idx = child.expect(['htop', pexpect.TIMEOUT], timeout=10)
    if idx != 0:
        print('LINUX_TEST_FAIL: htop not found')
        child.close(force=True); sys.exit(1)
    print('Verification Passed')
    child.close(force=True)
except Exception as e:
    print(f'LINUX_TEST_FAIL: {e}')
    try: child.close(force=True)
    except: pass
    sys.exit(1)
PYTEST

    local rc=$?
    if [[ ${rc} -eq 0 ]]; then
        echo -e "${GREEN}Check linux_4vcpu_1partion_riscv64 PASS${NC}"
        return 0
    else
        echo -e "${RED}Check linux_4vcpu_1partion_riscv64 FAILED${NC}"
        return 1
    fi
}

# Run the Mixed-OS demo test (riscv64 only)
# FreeRTOS para-virt on vCPU3 + Linux on vCPU0-2.
function run_test_mix_os_demo_riscv64() {
    local test_dir="${MONOREPO_ROOT}/user/bail/examples/mix_os_demo_riscv64"
    if [[ ! -d "${test_dir}" ]]; then
        echo -e "${RED}Test directory not found: ${test_dir}${NC}"
        return 1
    fi

    echo "+++ Checking examples/mix_os_demo_riscv64 [${ARCH}]"
    cd "${test_dir}"

    make clean > /dev/null 2>&1
    make > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        echo -e "${RED}Check mix_os_demo_riscv64 FAILED${NC} (build error)"
        return 1
    fi

    riscv64-linux-gnu-objcopy -O binary -R .note -R .note.gnu.build-id -R .comment -S \
        resident_sw resident_sw.bin

    python3 -u << 'PYTEST' 2>&1
import pexpect, sys, time
child = pexpect.spawn(
    'qemu-system-riscv64 '
    '-machine virt -cpu rv64 -smp 4 -m 1G '
    '-nographic -no-reboot '
    '-bios default -kernel resident_sw.bin '
    '-monitor none -serial stdio',
    timeout=400, encoding='utf-8', codec_errors='replace'
)
try:
    # Wait for Linux login prompt (RTOS output may be interleaved)
    idx = child.expect(['buildroot login:', pexpect.TIMEOUT, pexpect.EOF], timeout=380)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: login prompt not reached')
        child.close(force=True); sys.exit(1)
    boot_output = child.before if child.before else ''
    rtos_found = 'RTOS' in boot_output
    if not rtos_found:
        print('MIX_OS_TEST_FAIL: no FreeRTOS output on serial')
        child.close(force=True); sys.exit(1)
    print('FreeRTOS serial output detected')
    # Retry login up to 3 times in case of interleaved RTOS output
    logged_in = False
    for attempt in range(3):
        time.sleep(6)
        child.sendline('root')
        idx = child.expect(['assword', 'buildroot login:', pexpect.TIMEOUT], timeout=60)
        if idx == 0:
            time.sleep(2); child.sendline('1234')
            idx2 = child.expect([r'[\$#] ', 'Login incorrect', pexpect.TIMEOUT], timeout=60)
            if idx2 == 0:
                logged_in = True
                break
            elif idx2 == 1:
                print('MIX_OS_TEST_FAIL: login incorrect')
                child.close(force=True); sys.exit(1)
        elif idx == 1:
            continue
    if not logged_in:
        print('MIX_OS_TEST_FAIL: login failed after retries')
        child.close(force=True); sys.exit(1)
    time.sleep(2); child.sendline('nproc')
    idx = child.expect(['3', pexpect.TIMEOUT], timeout=15)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: nproc did not return 3')
        child.close(force=True); sys.exit(1)
    child.expect([r'[\$#] ', pexpect.TIMEOUT], timeout=10)
    child.sendline('cat /proc/cpuinfo | grep processor | wc -l')
    idx = child.expect(['3', pexpect.TIMEOUT], timeout=15)
    if idx != 0:
        print('MIX_OS_TEST_FAIL: cpuinfo does not show 3 CPUs')
        child.close(force=True); sys.exit(1)
    print('Verification Passed')
    child.close(force=True)
except Exception as e:
    print(f'MIX_OS_TEST_FAIL: {e}')
    try: child.close(force=True)
    except: pass
    sys.exit(1)
PYTEST

    local rc=$?
    if [[ ${rc} -eq 0 ]]; then
        echo -e "${GREEN}Check mix_os_demo_riscv64 PASS${NC}"
        return 0
    else
        echo -e "${RED}Check mix_os_demo_riscv64 FAILED${NC}"
        return 1
    fi
}

# Run a single test case
# Arguments: case_name
# Returns: 0 on PASS, 1 on FAIL
function run_test() {
    local case_name="$1"

    # Custom test runners for special cases
    if [[ "${case_name}" == "linux" ]]; then
        run_test_linux
        return $?
    fi
    if [[ "${case_name}" == "linux_4vcpu_1partion" ]]; then
        run_test_linux_4vcpu_1partion
        return $?
    fi
    if [[ "${case_name}" == "freertos_hw_virt" ]]; then
        run_test_freertos_hw_virt
        return $?
    fi
    if [[ "${case_name}" == "freertos_hw_virt_riscv" ]]; then
        run_test_freertos_hw_virt_riscv
        return $?
    fi
    if [[ "${case_name}" == "mix_os_demo1" ]]; then
        run_test_mix_os_demo1
        return $?
    fi
    if [[ "${case_name}" == "linux_4vcpu_1partion_riscv64" ]]; then
        run_test_linux_4vcpu_1partion_riscv64
        return $?
    fi
    if [[ "${case_name}" == "mix_os_demo_riscv64" ]]; then
        run_test_mix_os_demo_riscv64
        return $?
    fi

    if ! lookup_case "${case_name}"; then
        echo -e "${RED}Unknown test case: ${case_name}${NC}"
        return 1
    fi

    local test_dir="${MONOREPO_ROOT}/user/bail/examples/${case_name}"
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
        check-helloworld)     echo "helloworld" ;;
        check-helloworld_smp) echo "helloworld_smp" ;;
        check-freertos_para_virt)       echo "freertos_para_virt" ;;
        check-freertos_hw_virt) echo "freertos_hw_virt" ;;
        check-freertos_para_virt_riscv) echo "freertos_para_virt_riscv" ;;
        check-freertos_hw_virt_riscv) echo "freertos_hw_virt_riscv" ;;
        check-linux)          echo "linux" ;;
        check-linux_4vcpu_1partion) echo "linux_4vcpu_1partion" ;;
        check-linux_4vcpu_1partion_riscv64) echo "linux_4vcpu_1partion_riscv64" ;;
        check-mix_os_demo1) echo "mix_os_demo1" ;;
        check-mix_os_demo_riscv64) echo "mix_os_demo_riscv64" ;;
        check-[0-9]*)         echo "example.${builder#check-}" ;;
        *)                    echo "" ;;
    esac
}

# ---- Main ----

case "${BUILDER}" in
check-all)
    clean
    echo "+++ Building PRTOS for ${ARCH}"
    build_prtos

    for entry in "${ALL_CASES[@]}"; do
        case_name="${entry%%:*}"
        lookup_case "${case_name}"
        # Skip tests restricted to a different architecture
        # CASE_ARCH is a comma-separated list of allowed architectures
        if [[ -n "${CASE_ARCH}" ]] && ! echo ",${CASE_ARCH}," | grep -q ",${ARCH},"; then
            record_result "${case_name}" "SKIP"
            continue
        fi
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
        echo "Error: '${BUILDER}' is not a known test command"
        usage
        exit 1
    fi

    clean
    echo "+++ Building PRTOS for ${ARCH}"
    build_prtos

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

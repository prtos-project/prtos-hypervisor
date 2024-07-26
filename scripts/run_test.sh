#!/usr/bin/env bash

set -o pipefail
unset LANG
unset LC_ALL
unset LC_COLLATE

PROGNAME="$(basename "${0}")"

function usage() {
cat <<EOF
Usage:
${PROGNAME} [options] <BUILDER>

[-h|--help]         Display this help and exit.

check-<case name>   Check the specific test case.
                    helloworld, example.001, example.002, example.003, example.004,
                    example.005, example.006, example.007, example.008
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
        *)
            BUILDER="${1}"
            shift
            ;;
    esac
done

MONOREPO_ROOT="${MONOREPO_ROOT:="$(git rev-parse --show-toplevel)"}"
MAKE="make"
QEMU="qemu-system-i386"


function clean() {
    cd "${MONOREPO_ROOT}"
    ${MAKE} distclean
    #git checkout "${MONOREPO_ROOT}"
    #git clean -df
}

function build_prtos() {
    cd "${MONOREPO_ROOT}"
    cp prtos_config.x86 prtos_config
    ${MAKE} defconfig
    ${MAKE}
}

case "${BUILDER}" in
check-helloworld)
    clean
    echo "+++ Checking examples/helloworld"
    build_prtos
    TEST_CASE="helloworld"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 8
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-001)
    clean
    echo "+++ Checking examples/example.001"
    build_prtos
    TEST_CASE="example.001"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 15
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 2 ]; then
        echo "++++ Check ${TEST_CASE} PASS"
    else
        echo "++++ Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-002)
    clean
    echo "+++ Checking examples/example.002"
    build_prtos
    TEST_CASE="example.002"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 8
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-003)
    clean
    echo "+++ Checking examples/example.003"
    build_prtos
    TEST_CASE="example.003"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 12
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 3 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-004)
    clean
    echo "+++ Checking examples/example.004"
    build_prtos
    TEST_CASE="example.004"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 15
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-005)
    clean
    echo "+++ Checking examples/example.005"
    build_prtos
    TEST_CASE="example.005"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 8
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-006)
    clean
    echo "+++ Checking examples/example.006"
    build_prtos
    TEST_CASE="example.006"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 20
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-007)
    clean
    echo "+++ Checking examples/example.007"
    build_prtos
    TEST_CASE="example.007"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 40
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 1 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;
check-008)
    clean
    echo "+++ Checking examples/example.008"
    build_prtos
    TEST_CASE="example.008"
    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${TEST_CASE}"
    cd "${TEST_DIR}"
    make run.x86.nographic > "${TEST_DIR}"/"${TEST_CASE}".output 2>&1 &
    sleep 10
    killall -9 "${QEMU}"
    HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${TEST_CASE}".output | wc -l`
    if [ $HALTED_NUM == 2 ]; then
        echo "Check ${TEST_CASE} PASS"
    else
        echo "Check ${TEST_CASE} FAILED"
        exit 1
    fi
    clean
;;

check-all)
    clean
    echo "+++ Checking all examples"
	build_prtos
	declare -a arr=("example.001" "example.002" "example.003" "example.004" "example.005" "example.006" "example.007" "example.008" "example.009" "helloworld" "helloworld_smp")

    declare -a files=(2 1 3 1 1 1 1 2 2 1 2)
	declare -a timeouts=(20 8 12 15 8 20 40 15 15 15 15)
	
	## now loop through the above array
    index=0
	for case_name in "${arr[@]}"
	do
        limit=${files[$index]}
        timeout=${timeouts[$index]}
		index=`expr $index + 1`

	    TEST_DIR="${MONOREPO_ROOT}"/user/bail/examples/"${case_name}"
		cd "${TEST_DIR}"
		rm "${TEST_DIR}"/"${case_name}".output -rf
	    # or do whatever with individual element of the array
        make run.x86.nographic > "${TEST_DIR}"/"${case_name}".output 2>&1 &
        sleep ${timeout}
        killall -9 "${QEMU}"
        HALTED_NUM=`grep "Verification Passed$" "${TEST_DIR}"/"${case_name}".output | wc -l`
        if [ $HALTED_NUM == $limit ]; then
            echo "Check ${case_name} PASS"
        else
            echo "Check ${case_name} FAILED"
        fi
	done

;;

*)
    echo "${BUILDER} is not a known configuration"
    exit 1
;;
esac

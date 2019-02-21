#!/bin/bash
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
RESET='\033[0m' # No Color

# Debug off by default.
DEBUG=""

# Pick the release ebbrt install.
EBBRT_SYSROOT="/home/jmcadden/seuss/local/ebbrt"

# The filesystem.
NODE_FS=../ext/incubator-openwhisk-runtime-nodejs/core/nodejsAction.fs

# The root dir of what will become the filesystem
NODE_FS_ROOT=../ext/incubator-openwhisk-runtime-nodejs/core/nodejsActionBase/

# Option to rebuild Umm before running any tests.
BUILD_UMM=

# Option to send output here.
FILE=/dev/null

# Non gdb run.
RUN_TYPE=run

while getopts "bdhl:t:p:" OPTION; do
    case $OPTION in
        b)
            BUILD_UMM=1
            ;;
        d)
            printf "${RED}Debug / GDB build / run${RESET}\n"
            DEBUG=1
            EBBRT_SYSROOT=/home/jmcadden/seuss/local/ebbrt_debug
            RUN_TYPE=gdbrun
            ;;
        l)
            printf "${CYAN}Also sending output to file: $OPTARG ${RESET}\n"
            FILE=$OPTARG
            ;;
        t)
            TEST=$OPTARG
            ;;
        p)
            TEST_DIR=$OPTARG
            ;;
        h)
            echo b: build umm before tests run.
            echo d: debug mode
            echo f: force umm rebuild between tests why?
            echo h: this menu
            echo l <file>: log run to file
            echo t <dir> : directory to find test.
            exit 0
            ;;
        *)
            echo "Incorrect options provided"
            exit 1
            ;;
    esac
done

ALL_TESTS=(solo5-hello                      \
               solo5-nop                    \
               rump-simple-hello            \
               rump-nop nop.js              \
               nop_with_snap.js             \
               hello.js                     \
               hello_with_snap.js           \
               app_time_before_warm_snap.js \
               app_time_after_warm_snap.js  \
               app_time_before_hot_snap.js)

# Does that dir exist?
if [ ! -d "$TEST_DIR" ]
then
    echo dir: \"$TEST_DIR\" does not exist.
    echo error: you must use the -p flag to specify an existing test dir.
    exit 1
fi

# Does that test exist?
if [[ ! " ${ALL_TESTS[@]} " =~ " ${TEST} " ]];
then
    echo unknown test \"$TEST\" specify one of the folloing with -t:
    for i in "${ALL_TESTS[@]}"; do
        echo $i
    done
    exit 1
fi


# Build Umm if necessary.
if [ -n "$BUILD_UMM" ]
then
    # Force one time UMM rebuild.
    printf "${RED}Cleaning up Umm${RESET}\n"
    make clean -C ../
    printf "${RED}Making Umm${RESET}\n"
    make -j -C ../ || exit 1
    printf "${RED}Done UMM Build${RESET}\n"
fi


function run(){
    # Info on run.
    printf "${CYAN}DEBUG is \"$DEBUG\" TARGET is \"$TARGET\"${RESET}\n"
    printf "${CYAN}EBBRT_SYSROOT is \"$EBBRT_SYSROOT\"${RESET}\n"

    printf "${GREEN}Removing Test${RESET}\n"
    make -C ${TEST_DIR} clean

    printf "${GREEN}Building Test${RESET}\n"
    echo target is $1
    TARGET=$1 make -C ${TEST_DIR} -j || exit 1

    printf "${GREEN}Running Test${RESET}\n"
    make -C ${TEST_DIR} $RUN_TYPE #2>&1 | tee ${FILE}
}

function runTest(){
    # Run with target from makefile.
    echo in runTest arg is $1
    run $1
}

function runJSTest(){
    # Hack to drive from this script.
    # Assumes umm-solo5.h contains:
    #const std::string opts_ = R"({"cmdline":"bin/node-default /nodejsActionBase/myapp.js"})";

    # Blow away old fs.
    if [ -f "$NODE_FS" ]; then
        rm $NODE_FS
    fi

    # Move js app to this tmp file.
    cp ${NODE_FS_ROOT}$1 ${NODE_FS_ROOT}myapp.js

    run nodejsAction

    # Remove tmp file.
    if [ -f "$NODE_FS_ROOT"/myapp.js ]; then
        rm ${NODE_FS_ROOT}myapp.js
    fi
}


# VARIOUS TESTS. FIRST ARGUMENT IS MAKE TARGET.

# A C hello for solo.
# runTest solo5-hello

# A C nop for solo.
# runTest solo5-nop

# A C hello after rump boot.
# runTest rump-simple-hello

# A C nop for rump.
# runTest rump-nop

# An empty js fn.
# runJSTest nop.js

# An empty js fn.
# runJSTest nop_with_snap.js

# A js hello.
# runJSTest hello.js

# A js hello.
# runJSTest hello_with_snap.js

# Our listener app exiting right before the warm start call to uptime.
# runJSTest app_time_before_warm_snap.js

# Our listener app exiting right before the warm start call to uptime.
# runJSTest app_time_after_warm_snap.js

# Our listener app exiting right after snapshotting warm uptime call.
# runJSTest app_time_before_hot_snap.js

# Are we doing a js test?
if [[ $TEST == *.js ]]
then
    runJSTest $TEST
else
    runTest $TEST
fi


echo end of profile script

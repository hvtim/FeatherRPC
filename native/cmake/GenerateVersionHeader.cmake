# Run as a `cmake -P` script-mode custom target on *every* build (see the
# add_custom_target(generate_version ALL ...) call in CMakeLists.txt) -
# not something that only reruns on a full reconfigure, since the git
# commit hash can change between builds without anything CMake-relevant
# changing (no CMakeLists.txt edit, no new/removed source file).
#
# Expected variables (passed via -D on the command line):
#   TEMPLATE  - path to Version.h.in
#   DST       - path to the generated Version.h to produce
#   SRC_DIR   - repo root to run `git` in
#   VERSION   - the FEATHERRPC_VERSION value

find_package(Git QUIET)

set(FEATHERRPC_GIT_HASH "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE GIT_HASH_OUTPUT
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_RESULT EQUAL 0 AND NOT GIT_HASH_OUTPUT STREQUAL "")
        set(FEATHERRPC_GIT_HASH "${GIT_HASH_OUTPUT}")
    endif()
    # A dirty working tree (uncommitted changes) matters for a dev build -
    # a crash report citing a bare hash could otherwise point a maintainer
    # at code that isn't actually what produced the crash.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE GIT_DIRTY_OUTPUT
        RESULT_VARIABLE GIT_DIRTY_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_DIRTY_RESULT EQUAL 0 AND NOT GIT_DIRTY_OUTPUT STREQUAL "")
        set(FEATHERRPC_GIT_HASH "${FEATHERRPC_GIT_HASH}-dirty")
    endif()
endif()

set(FEATHERRPC_VERSION "${VERSION}")

configure_file(${TEMPLATE} ${DST}.tmp @ONLY)

# Only overwrite the real header if content actually changed - avoids
# forcing a rebuild of every translation unit that includes Version.h on
# every single build when the hash hasn't moved (e.g. rerunning a build
# with no new commits in between).
set(SHOULD_COPY TRUE)
if(EXISTS ${DST})
    file(READ ${DST} EXISTING_CONTENT)
    file(READ ${DST}.tmp NEW_CONTENT)
    if(EXISTING_CONTENT STREQUAL NEW_CONTENT)
        set(SHOULD_COPY FALSE)
    endif()
endif()

if(SHOULD_COPY)
    file(RENAME ${DST}.tmp ${DST})
else()
    file(REMOVE ${DST}.tmp)
endif()

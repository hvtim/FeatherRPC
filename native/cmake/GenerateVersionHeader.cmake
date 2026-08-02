# Runs on every build, not just reconfigure (see generate_version target
# in CMakeLists.txt) - the git hash can change with no CMake-relevant change.
#
# Variables (passed via -D):
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

# Only overwrite if content changed, to avoid forcing an unnecessary rebuild.
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

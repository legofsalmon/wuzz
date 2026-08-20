# Regenerates the version header at BUILD time, not configure time, so the stamped
# git SHA always matches the commit actually being compiled - a configure-time
# stamp silently goes stale on every commit until someone happens to re-run cmake.
#
# Inputs: ND_VERSION, ND_TEMPLATE, ND_OUTPUT, ND_SOURCE_DIR

execute_process(
    COMMAND git rev-parse --short=9 HEAD
    WORKING_DIRECTORY "${ND_SOURCE_DIR}"
    OUTPUT_VARIABLE ND_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE ND_GIT_RESULT)

if(NOT ND_GIT_RESULT EQUAL 0 OR ND_GIT_SHA STREQUAL "")
    set(ND_GIT_SHA "unknown")
endif()

execute_process(
    COMMAND git status --porcelain
    WORKING_DIRECTORY "${ND_SOURCE_DIR}"
    OUTPUT_VARIABLE ND_GIT_DIRTY_OUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT ND_GIT_DIRTY_OUT STREQUAL "")
    set(ND_GIT_SHA "${ND_GIT_SHA}*")   # uncommitted changes: the SHA alone would lie
endif()

# configure_file only rewrites on content change, so an unchanged SHA does not
# trigger a rebuild of everything that includes the header.
configure_file("${ND_TEMPLATE}" "${ND_OUTPUT}" @ONLY)

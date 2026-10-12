# Writes the shim's version header; runs on every build so local builds name
# the commit they were built from. Rewritten only when it changes.
#   VERSION     release tag (vYYYY.M.D or vYYYY.M.D-N); empty for local builds
#   GIT         git executable, may be empty
#   SOURCE_DIR  the repository
#   OUTPUT      header to write
set(commit "unknown")
set(described "")
if(GIT)
  execute_process(COMMAND "${GIT}" rev-parse --short=7 HEAD WORKING_DIRECTORY "${SOURCE_DIR}"
                  OUTPUT_VARIABLE head RESULT_VARIABLE failed OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT failed AND head)
    set(commit "${head}")
    # Tracked changes only; build output and SDK folders are untracked.
    execute_process(COMMAND "${GIT}" status --porcelain --untracked-files=no WORKING_DIRECTORY "${SOURCE_DIR}"
                    OUTPUT_VARIABLE changes ERROR_QUIET)
    if(changes)
      string(APPEND commit "-dirty")
    endif()
    execute_process(COMMAND "${GIT}" describe --tags --match "v*" --dirty WORKING_DIRECTORY "${SOURCE_DIR}"
                    OUTPUT_VARIABLE described RESULT_VARIABLE failed OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(failed)
      set(described "")
    endif()
  endif()
endif()
# Windows file versions are four numbers; only release tags have them.
set(numbers "0,0,0,0")
if(VERSION)
  if(NOT VERSION MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9]+))?$")
    message(FATAL_ERROR "Release version '${VERSION}' is not vYYYY.M.D or vYYYY.M.D-N")
  endif()
  set(revision 0)
  if(CMAKE_MATCH_5)
    set(revision "${CMAKE_MATCH_5}")
  endif()
  set(numbers "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},${revision}")
elseif(described)
  set(VERSION "${described}") # e.g. v2026.10.12-3-2-gf9fa49f-dirty: two commits after a release
else()
  set(VERSION "dev")
endif()
set(text "#pragma once
#define CTR2SHIM_VERSION \"${VERSION}\"
#define CTR2SHIM_COMMIT \"${commit}\"
#define CTR2SHIM_VERSION_NUMBERS ${numbers}
#define CTR2SHIM_PRODUCT_VERSION \"${VERSION} (${commit})\"
")
set(previous "")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" previous)
endif()
if(NOT previous STREQUAL text)
  file(WRITE "${OUTPUT}" "${text}")
endif()

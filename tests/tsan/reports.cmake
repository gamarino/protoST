# The two ends of a ThreadSanitizer run (the `tsan` label, PROTOST_TSAN).
#
# Every test of the label runs with TSAN_OPTIONS log_path=<DIR>/report, so
# TSan writes each process's reports to <DIR>/report.<pid> instead of the
# test's stderr. That catches a race in any process: one whose test expects
# an error exit (where TSan's own exit code 66 would pass unnoticed), and a
# child process a CLI test starts.
#
#   cmake -DDIR=<dir> -DMODE=clear -P reports.cmake   empty <dir> (fixture setup)
#   cmake -DDIR=<dir> -DMODE=check -P reports.cmake   fail, printing them, if
#                                                     any report was written
if(NOT DEFINED DIR OR NOT DEFINED MODE)
    message(FATAL_ERROR "usage: cmake -DDIR=<dir> -DMODE=clear|check -P reports.cmake")
endif()
if(MODE STREQUAL "clear")
    file(REMOVE_RECURSE "${DIR}")
    file(MAKE_DIRECTORY "${DIR}")
elseif(MODE STREQUAL "check")
    file(GLOB reports "${DIR}/report.*")
    list(LENGTH reports count)
    if(count EQUAL 0)
        message(STATUS "ThreadSanitizer: no report in ${DIR}")
        return()
    endif()
    foreach(report IN LISTS reports)
        file(READ "${report}" text)
        message("---- ${report}\n${text}")
    endforeach()
    message(FATAL_ERROR "ThreadSanitizer wrote ${count} report file(s) in ${DIR}")
else()
    message(FATAL_ERROR "MODE must be clear or check, not '${MODE}'")
endif()

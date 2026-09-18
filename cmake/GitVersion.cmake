# Stamps the current git short SHA (with a trailing '*' flag when the working
# tree is dirty) into a generated header. Run as a script (-P) from a custom
# target so it re-evaluates on every build, not just at configure time.
execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${SRC}
        OUTPUT_VARIABLE COWTERM_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

execute_process(
        COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${SRC}
        OUTPUT_VARIABLE _dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

if (NOT COWTERM_GIT_SHA)
    set(COWTERM_GIT_SHA "unknown")
endif ()

if (_dirty)
    set(COWTERM_GIT_DIRTY 1)
else ()
    set(COWTERM_GIT_DIRTY 0)
endif ()

# Only rewrite the header when something changed, so a clean rebuild doesn't
# force every dependent translation unit to recompile each time.
configure_file(${IN} ${OUT}.tmp @ONLY)

if (EXISTS ${OUT})
    file(READ ${OUT} _old)
    file(READ ${OUT}.tmp _new)
    if (_old STREQUAL _new)
        file(REMOVE ${OUT}.tmp)
        return()
    endif ()
endif ()

file(RENAME ${OUT}.tmp ${OUT})

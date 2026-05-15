if(NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "PATCH_FILE is required.")
endif()

execute_process(
    COMMAND git apply --check --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE APPLY_CHECK_RESULT)

if(APPLY_CHECK_RESULT EQUAL 0)
    execute_process(
        COMMAND git apply --ignore-whitespace "${PATCH_FILE}"
        RESULT_VARIABLE APPLY_RESULT)
    if(NOT APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}.")
    endif()
    return()
endif()

execute_process(
    COMMAND git apply --reverse --check --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE REVERSE_CHECK_RESULT)

if(NOT REVERSE_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "${PATCH_FILE} neither applies cleanly nor appears to be already applied.")
endif()

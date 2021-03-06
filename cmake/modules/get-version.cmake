cmake_minimum_required(VERSION 3.1.0)

set(VER_FILE ${PROJECT_SOURCE_DIR}/version)
unset(APP_VERSION)
unset(PSI_REVISION)
unset(PSI_PLUS_REVISION)
set(DEFAULT_VER "1.4")

function(read_version_file VF_RESULT)
    if(EXISTS "${VER_FILE}")
        message(STATUS "Found Psi version file: ${VER_FILE}")
        file(STRINGS "${VER_FILE}" VER_LINES)
        if(VER_LINES)
            set(${VF_RESULT} ${VER_LINES} PARENT_SCOPE)
        else()
            message(FATAL_ERROR "Can't read ${VER_FILE} contents")
        endif()
        unset(VER_LINES)
    else()
        message(FATAL_ERROR "${VER_FILE} not found")
    endif()
endfunction()

function(obtain_git_psi_version GIT_PSI_VERSION)
    if(EXISTS "${PROJECT_SOURCE_DIR}/\.git")
        find_program(GIT_BIN git DOC "Path to git utility")
        if(GIT_BIN)
            message(STATUS "Git utility found ${GIT_BIN}")
            execute_process(
                COMMAND ${GIT_BIN} describe --tags --abbrev=0
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE MAIN_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_VARIABLE ERROR_1
                )
            if(MAIN_VER)
                set(APP_VERSION "${MAIN_VER}" PARENT_SCOPE)
                set(${GIT_PSI_VERSION} ${MAIN_VER} PARENT_SCOPE)
            elseif(ERROR_1)
                message("Can't detect last tag ${ERROR_1}")
            endif()
        endif()
    endif()
endfunction()

function(obtain_git_psi_plus_version GIT_PSI_PLUS_VERSION)
    if(EXISTS "${PROJECT_SOURCE_DIR}/\.git")
        find_program(GIT_BIN git DOC "Path to git utility")
        if(GIT_BIN)
            obtain_git_psi_version(GIT_VER)
            if(GIT_VER)
                execute_process(
                    COMMAND ${GIT_BIN} rev-list --count ${GIT_VER}\.\.HEAD
                    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                    OUTPUT_VARIABLE COMMITS
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_VARIABLE ERROR_2
                    )
                if(COMMITS)
                    execute_process(
                        COMMAND ${GIT_BIN} rev-parse --short HEAD
                        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                        OUTPUT_VARIABLE VER_HASH
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_VARIABLE ERROR_3
                        )
                elseif(ERROR_2)
                    message("Can't count commits number: ${ERROR_2}")
                endif()
                if(ERROR_3)
                    message("Can't detect HEAD hash: ${ERROR_3}")
                endif()
                if(COMMITS AND VER_HASH)
                    set(PSI_REVISION ${VER_HASH} PARENT_SCOPE)
                    set(APP_VERSION "${GIT_VER}.${COMMITS}" PARENT_SCOPE)
                    set(${GIT_PSI_PLUS_VERSION} "${GIT_VER}.${COMMITS} \(${PSI_COMPILATION_DATE}, ${VER_HASH}${PSI_VER_SUFFIX}\)" PARENT_SCOPE)
                endif()
            endif()
        endif()
    endif()
endfunction()

function(obtain_psi_file_version VERSION_LINES OUTPUT_VERSION)
    string(REGEX MATCHALL "^([0-9]+(\\.[0-9]+)+)+(.+Psi:([a-fA-F0-9]+)?.+Psi\\+:([a-fA-F0-9]+)?)?" VER_LINE_A ${VERSION_LINES})
    if(${CMAKE_MATCH_COUNT} EQUAL 5)
        set(P_P ON)
        if(CMAKE_MATCH_1)
            set(_APP_VERSION ${CMAKE_MATCH_1})
        endif()
        if(CMAKE_MATCH_4)
            set(_PSI_REVISION ${CMAKE_MATCH_4})
        endif()
        if(CMAKE_MATCH_5)
            set(_PSI_PLUS_REVISION ${CMAKE_MATCH_5})
        endif()
    elseif(${CMAKE_MATCH_COUNT} EQUAL 2)
        set(P_P OFF)
        if(CMAKE_MATCH_1)
            set(_APP_VERSION ${CMAKE_MATCH_1})
        endif()
    endif()
    if(_APP_VERSION)
        set(APP_VERSION ${_APP_VERSION} PARENT_SCOPE)
        message(STATUS "Psi version found: ${_APP_VERSION}")
    else()
        message(WARNING "Psi+ version not found! ${DEFAULT_VER} version will be set")
        set(_APP_VERSION ${DEFAULT_VER})
        set(APP_VERSION ${_APP_VERSION} PARENT_SCOPE)
    endif()
    if(_PSI_REVISION)
        set(PSI_REVISION ${_PSI_REVISION} PARENT_SCOPE)
        message(STATUS "Psi revision found: ${_PSI_REVISION}")
    endif()
    if(_PSI_PLUS_REVISION)
        set(PSI_PLUS_REVISION ${_PSI_PLUS_REVISION} PARENT_SCOPE)
        message(STATUS "Psi+ revision found: ${_PSI_PLUS_REVISION}")
    endif()
    if(_APP_VERSION AND (_PSI_REVISION AND _PSI_PLUS_REVISION ))
        set(${OUTPUT_VERSION} "${_APP_VERSION} \(${PSI_COMPILATION_DATE}, Psi:${_PSI_REVISION}, Psi+:${_PSI_PLUS_REVISION}${PSI_VER_SUFFIX}\)" PARENT_SCOPE)
    else()
        if(PRODUCTION)
            set(${OUTPUT_VERSION} "${_APP_VERSION}" PARENT_SCOPE)
        else()
            set(${OUTPUT_VERSION} "${_APP_VERSION} \(${PSI_COMPILATION_DATE}${PSI_VER_SUFFIX}\)" PARENT_SCOPE)
        endif()
    endif()
endfunction()

if(NOT PSI_VERSION)
    if(IS_PSIPLUS)
        obtain_git_psi_plus_version(GITVER)
        if(GITVER)
            message(STATUS "Git Version ${GITVER}")
            set(PSI_VERSION "${GITVER}")
        else()
            read_version_file(VER_LINES)
            obtain_psi_file_version("${VER_LINES}" FILEVER)
            if(FILEVER)
                set(PSI_VERSION "${FILEVER}")
            endif()
        endif()
    else()
        obtain_git_psi_version(GITVER)
        if(GITVER)
            message(STATUS "Git Version ${GITVER}")
            if(PRODUCTION)
                set(PSI_VERSION "${GITVER}")
            else()
                set(PSI_VERSION "${GITVER} \(${PSI_COMPILATION_DATE}${PSI_VER_SUFFIX}\)")
            endif()
        else()
            read_version_file(VER_LINES)
            obtain_psi_file_version("${VER_LINES}" FILEVER)
            if(FILEVER)
                set(PSI_VERSION "${FILEVER}")
            endif()
        endif()
    endif()
    unset(VER_LINES)
endif()

message(STATUS "${CLIENT_NAME} version set: ${PSI_VERSION}")

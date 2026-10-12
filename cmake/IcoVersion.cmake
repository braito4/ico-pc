# cmake/IcoVersion.cmake: the numbers of the Windows version information.
#
# ico_version_from_label(<label> <numeric-var> <text-var>) turns a package
# label into "x,y,z,N" (the FILEVERSION form) and "x.y.z.N". The label must
# start with [v]x.y.z; N is the release candidate's number for a label such as
# v0.6.2-rc3 (0.6.2.3) and 0 for the final (0.6.2.0). Any other label (a
# development build) gives 0,0,0,0 and 0.0.0.0.
# tools/test/package_textures_test.sh runs this file's cases with `cmake -P`.
function(ico_version_from_label label out_numeric out_text)
    if(label MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(_ver "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
        set(_rc 0)
        if(label MATCHES "^v?[0-9]+\\.[0-9]+\\.[0-9]+-rc([0-9]+)")
            set(_rc "${CMAKE_MATCH_1}")
        endif()
        string(REPLACE "." "," _commas "${_ver}")
        set(${out_numeric} "${_commas},${_rc}" PARENT_SCOPE)
        set(${out_text} "${_ver}.${_rc}" PARENT_SCOPE)
    else()
        set(${out_numeric} "0,0,0,0" PARENT_SCOPE)
        set(${out_text} "0.0.0.0" PARENT_SCOPE)
    endif()
endfunction()

# cmake -P cmake/IcoVersion.cmake: print "<label> <numeric> <text>" per label in -DLABELS=a;b
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    foreach(_l IN LISTS LABELS)
        ico_version_from_label("${_l}" _n _t)
        message(STATUS "${_l} ${_n} ${_t}")
    endforeach()
endif()

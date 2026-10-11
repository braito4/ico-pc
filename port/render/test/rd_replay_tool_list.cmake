# rd_replay_tool_list: rd_replay_tool --list --no-device on
# the dump rd_pixel leaves behind loads it without a device, renders
# nothing, exits 0 and prints one line per command ("L:INDEX TYPE key ...")
# and the count.  -DTOOL=<rd_replay_tool> -DDUMP=<rddump>
if(NOT EXISTS "${DUMP}")
  # ctest's SKIP_REGULAR_EXPRESSION maps this to Not Run
  message(STATUS "SKIP: ${DUMP}: no dump (rd_pixel skipped)")
  return()
endif()
execute_process(COMMAND "${TOOL}" "${DUMP}" - --list --no-device
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "rd_replay_tool --list --no-device exited ${rc}: ${err}")
endif()
string(REGEX MATCHALL "(^|\n) ?[0-9]+:[0-9]+ +[A-Z_]+ +key [0-9a-f]+" lines "${out}")
list(LENGTH lines n)
# the dump's header line: version, GS size, aspect, visible and cull width
if(NOT out MATCHES "dump: version [0-9]+, GS [0-9]+x[0-9]+, aspect [0-9.]+ \\([^)]+\\), visible width [0-9.]+, cull width [0-9]+")
  message(FATAL_ERROR "no 'dump: version ...' line in:\n${out}")
endif()
string(REGEX MATCH "listed ([0-9]+) commands" summary "${out}")
if(NOT summary)
  message(FATAL_ERROR "no summary line in:\n${out}")
endif()
set(want "${CMAKE_MATCH_1}")
if(NOT n EQUAL want OR want EQUAL 0)
  message(FATAL_ERROR "${n} command lines, the tool listed ${want}")
endif()
message(STATUS "rd_replay_tool_list: ${n} commands, one line each")

set(SUGARAT_LIGHTING_GROUP "1" CACHE STRING "Group shared by one Sugarat Reactor and its side displays")
if(NOT SUGARAT_LIGHTING_GROUP MATCHES "^[1-9][0-9]*$" OR SUGARAT_LIGHTING_GROUP GREATER 65535)
    message(FATAL_ERROR "SUGARAT_LIGHTING_GROUP must be an integer from 1 through 65535")
endif()

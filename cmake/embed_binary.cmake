if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "INPUT, OUTPUT, and SYMBOL are required")
endif()

file(READ "${INPUT}" bytes HEX)
file(SIZE "${INPUT}" byte_count)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
file(WRITE "${OUTPUT}"
    "unsigned char ${SYMBOL}[] = {${bytes}};\n"
    "unsigned int ${SYMBOL}_len = ${byte_count};\n"
)

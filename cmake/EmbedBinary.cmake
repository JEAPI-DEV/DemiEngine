if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "EmbedBinary requires INPUT, OUTPUT, and SYMBOL")
endif()

file(READ "${INPUT}" BINARY_HEX HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," BINARY_BYTES
  "${BINARY_HEX}")

get_filename_component(OUTPUT_DIRECTORY "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
file(WRITE "${OUTPUT}"
  "#pragma once\n"
  "#include <cstdint>\n"
  "namespace demi::runtime::render {\n"
  "inline constexpr std::uint8_t ${SYMBOL}[] = {${BINARY_BYTES}};\n"
  "} // namespace demi::runtime::render\n")

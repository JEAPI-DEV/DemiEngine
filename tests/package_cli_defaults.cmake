# Exercise defaults without depending on DNS or the public registry:
# an empty dependency graph records the chosen registry without downloading.
string(RANDOM LENGTH 12 ALPHABET abcdef0123456789 suffix)
set(root "${CMAKE_CURRENT_BINARY_DIR}/package-cli-defaults-${suffix}")
file(MAKE_DIRECTORY "${root}/project" "${root}/elsewhere" "${root}/registry")
set(project "${root}/project/demi.project.json")
file(WRITE "${project}" "{\"format_version\":1,\"packages\":{}}")

function(install_expect expected)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DEMI_PACKAGE_REGISTRY
    "${DEMI}" package install ${ARGN}
    WORKING_DIRECTORY "${root}/project" RESULT_VARIABLE code
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "Install failed: ${output} ${error}")
  endif()
  file(READ "${root}/project/demi.packages.lock.json" lock)
  string(JSON actual GET "${lock}" registry)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Expected registry ${expected}, got ${actual}")
  endif()
endfunction()

install_expect("https://demiengine.de")
file(WRITE "${project}" "{\"format_version\":1,\"packages\":{},\"package_registry\":\"../registry\"}")
install_expect("../registry")
install_expect("${root}/registry" --registry "${root}/registry")
file(WRITE "${project}" "{\"format_version\":1,\"packages\":{}}")
install_expect("${root}/registry" --locked)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "DEMI_PACKAGE_REGISTRY=${root}/registry"
  "${DEMI}" package install --project "${project}"
  WORKING_DIRECTORY "${root}/elsewhere" RESULT_VARIABLE code ERROR_VARIABLE error)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "Explicit project/environment override failed: ${error}")
endif()
file(READ "${root}/project/demi.packages.lock.json" lock)
string(JSON actual GET "${lock}" registry)
if(NOT actual STREQUAL "${root}/registry")
  message(FATAL_ERROR "Environment registry was not selected")
endif()
execute_process(COMMAND "${DEMI}" package install WORKING_DIRECTORY "${root}/elsewhere"
  RESULT_VARIABLE code ERROR_VARIABLE error)
if(code EQUAL 0 OR NOT error MATCHES "PACKAGE_PROJECT_READ_FAILED")
  message(FATAL_ERROR "Missing project must fail clearly")
endif()

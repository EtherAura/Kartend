# AppImage packaging checks that run without an AppImage.
#
# AppImageExtractCacheSmoke runs packaging/appimage/smoke-test.sh's
# --check-extract-cache mode against small temporary stand-in executables,
# so it proves the checker itself; it builds, downloads and starts no
# AppImage. Linux only, like the AppImage.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Python3 COMPONENTS Interpreter)
  if (Python3_Interpreter_FOUND)
    add_test(NAME AppImageExtractCacheSmoke
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/appimage/tests/test_extract_cache.py -v)
    set_tests_properties(AppImageExtractCacheSmoke PROPERTIES TIMEOUT 240)
  else()
    message(STATUS "AppImageExtractCacheSmoke: no Python 3 interpreter, test not registered")
  endif()
endif()

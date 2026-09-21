cmake_minimum_required(VERSION 3.16)
project(se_refactor C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(LIBCURL REQUIRED libcurl)

include_directories(${CMAKE_SOURCE_DIR}/include ${LIBXML2_INCLUDE_DIRS}
                    ${LIBCURL_INCLUDE_DIRS})
link_directories(${LIBXML2_LIBRARY_DIRS} ${LIBCURL_LIBRARY_DIRS})

add_library(se_core
  src/val/val.c
  src/codec/xml_codec.c
  src/http/curl_client.c
  src/resource/resource.c
)
target_link_libraries(se_core PUBLIC ${LIBXML2_LIBRARIES} ${LIBCURL_LIBRARIES})
target_compile_options(se_core PUBLIC ${LIBXML2_CFLAGS_OTHER} ${LIBCURL_CFLAGS_OTHER})

add_executable(se_get src/app/se_get.c)
target_link_libraries(se_get PRIVATE se_core)

enable_testing()
add_executable(val_test tests/val_test.c)
target_link_libraries(val_test PRIVATE se_core)
add_test(NAME val_test COMMAND val_test)

add_executable(xml_codec_test tests/xml_codec_test.c)
target_link_libraries(xml_codec_test PRIVATE se_core)
add_test(NAME xml_codec_test COMMAND xml_codec_test)

add_executable(resource_expand_test tests/resource_expand_test.c)
target_link_libraries(resource_expand_test PRIVATE se_core)
add_test(NAME resource_expand_test COMMAND resource_expand_test)

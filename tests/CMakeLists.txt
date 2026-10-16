# @@@LICENSE
#
#      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

# The daemon's own objects, linked with an in-process luna-service2
# (fakes/fakebus.cpp) instead of the real library, and with json-c wrapped
# (fakes/fakejson.cpp) so its allocations can be made to fail.

find_package(GTest REQUIRED)

set(BA_SANITIZE "" CACHE STRING
    "Sanitizers for the test build, as -fsanitize= takes them (address,undefined or thread)")
set(BA_NO_SANITIZE "" CACHE STRING
    "Checks to leave out of BA_SANITIZE, as -fno-sanitize= takes them")

add_executable(backupagent-tests
    $<TARGET_OBJECTS:backupagent-core>
    fakes/fakebus.cpp
    fakes/fakejson.cpp
    unit/test_files.cpp
    unit/test_prebackup.cpp
    unit/test_postrestore.cpp
    unit/test_lifecycle.cpp
    unit/test_daemon.cpp)

target_include_directories(backupagent-tests PRIVATE fakes unit)
target_compile_definitions(backupagent-tests PRIVATE BA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(backupagent-tests PRIVATE -Wextra)

# No luna-service2 here: fakebus.cpp is the only one.
target_link_libraries(backupagent-tests
    GTest::gtest_main
    GTest::gtest
    ${GLIB2_LDFLAGS}
    ${JSON_LDFLAGS}
    ${CMAKE_DL_LIBS}
    pthread)

if (BA_SANITIZE)
    set(BA_SANITIZE_LINK -fsanitize=${BA_SANITIZE})
    if (BA_NO_SANITIZE)
        list(APPEND BA_SANITIZE_LINK -fno-sanitize=${BA_NO_SANITIZE})
    endif()
    foreach(t backupagent-core backupagent-tests)
        target_compile_options(${t} PRIVATE ${BA_SANITIZE_LINK} -fno-omit-frame-pointer
                               -fno-sanitize-recover=all)
    endforeach()
    target_link_options(backupagent-tests PRIVATE ${BA_SANITIZE_LINK})
endif()

add_test(NAME backupagent-tests COMMAND backupagent-tests)

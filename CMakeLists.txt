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

cmake_minimum_required(VERSION 3.5)

project(luna-backupagent CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC3)
webos_component(1 0 0)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(LUNASERVICE2 REQUIRED luna-service2)
include_directories(${LUNASERVICE2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE2_CFLAGS})

pkg_check_modules(JSON REQUIRED json-c)
include_directories(${JSON_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${JSON_CFLAGS})

webos_add_compiler_flags(ALL -Wall)
webos_add_linker_options(ALL --no-undefined)

include_directories(Src)

add_executable(LunaBackupAgent
    Src/BackupAgent.cpp
    Src/Main.cpp)

target_link_libraries(LunaBackupAgent
    ${GLIB2_LDFLAGS}
    ${LUNASERVICE2_LDFLAGS}
    ${JSON_LDFLAGS}
    pthread)

webos_build_daemon(NAME LunaBackupAgent)
webos_build_system_bus_files()

# The backup service walks /etc/palm/backup and calls preBackup on every
# service registered there; this file is how it finds us.
install(FILES files/backup/com.webos.service.backupagent
        DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/backup)

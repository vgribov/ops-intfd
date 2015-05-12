#Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
#All Rights Reserved.
#
#   Licensed under the Apache License, Version 2.0 (the "License"); you may
#   not use this file except in compliance with the License. You may obtain
#   a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#   License for the specific language governing permissions and limitations
#   under the License.
#
#
# src/l2/intfd/CMakeLists.txt

cmake_minimum_required (VERSION 2.8)

set (INTFD intfd)
project (${INTFD})

include(FindPkgConfig)
pkg_check_modules(CONFIG_YAML config-yaml)
pkg_check_modules(HALONUTILS halonutils)
pkg_check_modules(OVSCOMMON libovscommon)
pkg_check_modules(OVSDB libovsdb)

include_directories (${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}
   ${OVSCOMMON_INCLUDE_DIRS})


# Source files to build intfd
set (SOURCES intfd.c intfd_ovsdb_if.c intfd_utils.c)

# Rules to build intfd
add_executable (${INTFD} ${SOURCES})

target_link_libraries (${INTFD} ${HALONUTILS_LIBRARIES} ${CONFIG_YAML_LIBRARIES} ${OVSCOMMON_LIBRARIES}
                      ${OVSDB_LIBRARIES} ${OPENSSL_LIBRARIES}
                       -lpthread -lrt)

# Rules to install intfd binary in rootfs
install(TARGETS ${INTFD}
    RUNTIME DESTINATION bin)

# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/esp/v6.0/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "C:/esp/v6.0/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader"
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix"
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/tmp"
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/src"
  "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/jelop/OneDrive/Documentos/U/2026-1/Sistemas _en _tiempo _real/RST_2026-1/ProyectoFinal/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

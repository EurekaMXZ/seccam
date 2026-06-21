set(SECCAM_CORE_VENDOR_ROOT "" CACHE PATH "SDK root used only to reuse vendored third-party headers for seccam-core")

if(NOT SECCAM_CORE_VENDOR_ROOT)
  set(_seccam_core_vendor_candidates
    "${CMAKE_SOURCE_DIR}/host_tools/sdk/duo-buildroot-sdk-v2-v2.0.1"
    "${CMAKE_SOURCE_DIR}/host_tools/sdk/duo-buildroot-sdk-v2"
  )
  foreach(_seccam_core_candidate IN LISTS _seccam_core_vendor_candidates)
    if(EXISTS "${_seccam_core_candidate}/cvi_rtsp/service/3rdparty/nlohmann_json/include/nlohmann/json.hpp")
      set(SECCAM_CORE_VENDOR_ROOT "${_seccam_core_candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT SECCAM_CORE_VENDOR_ROOT)
  message(FATAL_ERROR "Unable to locate a vendor SDK root for seccam-core third-party headers.")
endif()

set(SECCAM_CORE_JSON_INCLUDE_DIR
  "${SECCAM_CORE_VENDOR_ROOT}/cvi_rtsp/service/3rdparty/nlohmann_json/include"
)
set(SECCAM_CORE_INIH_DIR
  "${SECCAM_CORE_VENDOR_ROOT}/cvi_mpi/3rdparty/inih"
)
set(SECCAM_RUNTIME_ROOT "$ENV{SECCAM_RUNTIME_ROOT}" CACHE PATH "Milk-V Duo runtime library root used by seccam-core")
if(NOT SECCAM_RUNTIME_ROOT)
  set(SECCAM_RUNTIME_ROOT "${CMAKE_SOURCE_DIR}/host_tools/runtime/v2.0.1" CACHE PATH "Milk-V Duo runtime library root used by seccam-core" FORCE)
endif()

if(NOT EXISTS "${SECCAM_CORE_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "Missing nlohmann/json.hpp under ${SECCAM_CORE_JSON_INCLUDE_DIR}")
endif()
if(NOT EXISTS "${SECCAM_CORE_INIH_DIR}/ini.c" OR NOT EXISTS "${SECCAM_CORE_INIH_DIR}/ini.h")
  message(FATAL_ERROR "Missing inih sources under ${SECCAM_CORE_INIH_DIR}")
endif()

add_library(seccam_core_ini STATIC
  "${SECCAM_CORE_INIH_DIR}/ini.c"
)

target_include_directories(seccam_core_ini
  PUBLIC
    "${SECCAM_CORE_INIH_DIR}"
)

target_compile_options(seccam_core_ini
  PRIVATE
    -Wall
    -Wextra
)

set(SECCAM_CORE_CVI_ENABLED OFF)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64" AND SECCAM_RUNTIME_ROOT AND
   EXISTS "${SECCAM_RUNTIME_ROOT}/usr/lib/libsys.so" AND
   EXISTS "${SECCAM_CORE_VENDOR_ROOT}/cvi_mpi/include/cvi_vi.h" AND
   EXISTS "${SECCAM_CORE_VENDOR_ROOT}/tdl_sdk/include/cvi_tdl.h")
  set(SECCAM_CORE_CVI_ENABLED ON)
endif()

if(NOT SECCAM_CORE_CVI_ENABLED)
  message(STATUS "Skipping seccam-core target: CVI backend requires riscv64 runtime and SDK headers.")
  return()
endif()

add_executable(seccam-core
  src/main.cpp
  src/types.cpp
  src/config.cpp
  src/runtime_state.cpp
  src/core_service.cpp
  src/detection_tracker.cpp
  src/recording_engine.cpp
  src/recording_catalog.cpp
  src/command_dispatcher.cpp
  src/ipc_server.cpp
  src/core_backend_cvi.cpp
)

target_include_directories(seccam-core
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${SECCAM_CORE_JSON_INCLUDE_DIR}"
)

if(SECCAM_CORE_CVI_ENABLED)
  target_include_directories(seccam-core
    PRIVATE
      "${SECCAM_CORE_VENDOR_ROOT}/tdl_sdk/include"
      "${SECCAM_CORE_VENDOR_ROOT}/cvi_mpi/include"
      "${SECCAM_CORE_VENDOR_ROOT}/cvi_mpi/include/linux"
      "${SECCAM_CORE_VENDOR_ROOT}/cvi_mpi/include/isp/cv181x"
      "${SECCAM_CORE_VENDOR_ROOT}/cvi_rtsp/include"
  )

  target_link_directories(seccam-core
    PRIVATE
      "${SECCAM_RUNTIME_ROOT}/usr/lib"
      "${SECCAM_RUNTIME_ROOT}/usr/lib/3rd"
      "${SECCAM_RUNTIME_ROOT}/lib"
  )

  target_compile_definitions(seccam-core
    PRIVATE
      SECCAM_CORE_WITH_CVI=1
      CV181X
      __CV181X__
      _MIDDLEWARE_V2_
      _LARGEFILE_SOURCE
      _LARGEFILE64_SOURCE
      _FILE_OFFSET_BITS=64
  )

  target_link_options(seccam-core
    PRIVATE
      -Wl,-rpath,/mnt/system/lib
      -Wl,-rpath,/mnt/system/usr/lib
      -Wl,-rpath,/mnt/system/usr/lib/3rd
  )
endif()

target_compile_features(seccam-core
  PRIVATE
    cxx_std_17
)

target_compile_options(seccam-core
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(seccam-core
  PRIVATE
    seccam_core_ini
    Threads::Threads
)

if(SECCAM_CORE_CVI_ENABLED)
  target_link_libraries(seccam-core
    PRIVATE
      atomic
      cvi_ive
      sns_full
      isp
      venc
      awb
      ae
      af
      cvi_bin
      cvi_bin_isp
      misc
      isp_algo
      sys
      vi
      vo
      vpss
      rgn
      gdc
      cvi_tdl
      opencv_core
      opencv_imgproc
      opencv_imgcodecs
      cvikernel
      cvimath
      cviruntime
      cvi_rtsp
      m
  )
endif()

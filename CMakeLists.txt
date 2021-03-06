########################################
# Basic Setup                          #
########################################
cmake_minimum_required (VERSION 2.6)
SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Usr install prefix")
project (MistServer)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

SET(SOURCE_DIR ${PROJECT_SOURCE_DIR})
SET(BINARY_DIR ${PROJECT_BINARY_DIR})
include_directories(${SOURCE_DIR})
include_directories(${BINARY_DIR} ${BINARY_DIR}/generated)

########################################
# Testing - Enable Tests               #
########################################
enable_testing()
include(CTest)

########################################
# Build Variables - Install Prefix     #
########################################
if (NOT CMAKE_INSTALL_PREFIX)
  set (CMAKE_INSTALL_PREFIX /usr)
endif()

########################################
# Build Variables - Release            #
########################################
if (RELEASE)
  set (RELEASE_RAW ${RELEASE})
else()
#get the bitlength of this system
  execute_process(COMMAND getconf LONG_BIT OUTPUT_VARIABLE RELEASE_RAW )
  set(RELEASE_RAW "Generic_${RELEASE_RAW}" ) 
endif()
string(STRIP ${RELEASE_RAW} RELEASE)
set(RELEASE \"${RELEASE}\" )

########################################
# Build Variables - Package Version    #
########################################
#See if we have a git repo to get the version from
execute_process(COMMAND git describe --tags OUTPUT_VARIABLE PACKAGE_VERSION_RAW ERROR_QUIET)
if (NOT PACKAGE_VERSION_RAW)
  execute_process(COMMAND cat VERSION OUTPUT_VARIABLE PACKAGE_VERSION_RAW ERROR_QUIET)
endif()
if (NOT PACKAGE_VERSION_RAW)
  set(PACKAGE_VERSION_RAW Unknown)
endif()
#strip off the trailing spaces and newline
string(STRIP "${PACKAGE_VERSION_RAW}" PACKAGE_VERSION)
set(PACKAGE_VERSION \"${PACKAGE_VERSION}\" ) 

########################################
# Build Variables - Debug              #
########################################
if (NOT DEBUG)
  set(DEBUG 4)
endif()

########################################
# Build Variables - Shared Memory      #
########################################
if (NOT DEFINED NOSHM )
  add_definitions(-DSHM_ENABLED=1)
endif()

if (DEFINED BIGMETA )
  add_definitions(-DBIGMETA=1)
endif()
if (NOT DEFINED NOSSL )
  add_definitions(-DSSL=1)
endif()

########################################
# Build Variables - Thread Names       #
########################################
if (DEFINED WITH_THREADNAMES )
  add_definitions(-DWITH_THREADNAMES=1)
endif()

########################################
# Build Variables - No Crash Check     #
########################################
if (DEFINED NOCRASHCHECK )
  add_definitions(-DNOCRASHCHECK=1)
endif()

########################################
# Build Variables - Prepare for Build  #
########################################
message("Builing release ${RELEASE} for version ${PACKAGE_VERSION} @ debug level ${DEBUG}")
add_definitions(-g -funsigned-char -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DDEBUG=${DEBUG} -DPACKAGE_VERSION=${PACKAGE_VERSION} -DRELEASE=${RELEASE})

########################################
# MistLib - Header Files               #
########################################
set(libHeaders
  lib/amf.h
  lib/auth.h
  lib/encode.h
  lib/bitfields.h
  lib/bitstream.h
  lib/checksum.h
  lib/config.h
  lib/defines.h
  lib/dtsc.h
  lib/flv_tag.h
  lib/h264.h
  lib/http_parser.h
  lib/downloader.h
  lib/json.h
  lib/langcodes.h
  lib/mp4_adobe.h
  lib/mp4_generic.h
  lib/mp4.h
  lib/mp4_ms.h
  lib/nal.h
  lib/ogg.h
  lib/procs.h
  lib/rtmpchunks.h
  lib/shared_memory.h
  lib/socket.h
  lib/stream.h
  lib/theora.h
  lib/timing.h
  lib/tinythread.h
  lib/ts_packet.h
  lib/util.h
  lib/vorbis.h
  lib/opus.h
)

########################################
# MistLib - Build                      #
########################################
add_library (mist 
  ${libHeaders}
  lib/amf.cpp
  lib/auth.cpp
  lib/encode.cpp
  lib/bitfields.cpp
  lib/bitstream.cpp
  lib/config.cpp
  lib/dtsc.cpp
  lib/dtscmeta.cpp
  lib/flv_tag.cpp
  lib/h264.cpp
  lib/http_parser.cpp
  lib/downloader.cpp
  lib/json.cpp
  lib/langcodes.cpp
  lib/mp4_adobe.cpp
  lib/mp4.cpp
  lib/mp4_generic.cpp
  lib/mp4_ms.cpp
  lib/nal.cpp
  lib/ogg.cpp
  lib/procs.cpp
  lib/rtmpchunks.cpp
  lib/shared_memory.cpp
  lib/socket.cpp
  lib/stream.cpp
  lib/theora.cpp
  lib/timing.cpp
  lib/tinythread.cpp
  lib/ts_packet.cpp
  lib/util.cpp
  lib/vorbis.cpp
  lib/opus.cpp
)
if (NOT APPLE)
  set (LIBRT -lrt)
endif()
target_link_libraries(mist 
  -lpthread 
  ${LIBRT}
)
if (NOT DEFINED NOSSL )
  target_link_libraries(mist mbedtls mbedx509 mbedcrypto)
endif()
install(
  FILES ${libHeaders}
  DESTINATION include/mist
)
install(
  TARGETS mist
  DESTINATION lib
)

########################################
# MistLib - Local Header Install       #
########################################
if (${CMAKE_VERSION} VERSION_LESS 3.3.0)
add_custom_command(OUTPUT ${BINARY_DIR}/mist/.headers
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E make_directory ${BINARY_DIR}/mist
  COMMAND cp
  ARGS    ${libHeaders} ${BINARY_DIR}/mist
  COMMAND touch
  ARGS    ${BINARY_DIR}/mist/.headers
  WORKING_DIRECTORY ${SOURCE_DIR}
  DEPENDS ${libHeaders}
)
else()
add_custom_command(OUTPUT ${BINARY_DIR}/mist/.headers
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E make_directory ${BINARY_DIR}/mist
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E copy ${libHeaders} ${BINARY_DIR}/mist
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E touch ${BINARY_DIR}/mist/.headers
  WORKING_DIRECTORY ${SOURCE_DIR}
  DEPENDS ${libHeaders}
)
endif()

########################################
# MistServer - Analysers               #
########################################
macro(makeAnalyser analyserName format)
  add_executable(MistAnalyser${analyserName}
    src/analysers/mist_analyse.cpp
    src/analysers/analyser.cpp
    src/analysers/analyser_${format}.cpp
    ${BINARY_DIR}/mist/.headers
  )
  set_target_properties(MistAnalyser${analyserName} 
    PROPERTIES COMPILE_DEFINITIONS "ANALYSERHEADER=\"analyser_${format}.h\"; ANALYSERTYPE=Analyser${analyserName}"
  )
  target_link_libraries(MistAnalyser${analyserName}
    mist
  )
  install(
    TARGETS MistAnalyser${analyserName}
    DESTINATION bin
  )
endmacro()

makeAnalyser(RTMP rtmp)
makeAnalyser(FLV flv)
makeAnalyser(DTSC dtsc)
makeAnalyser(MP4 mp4)
makeAnalyser(OGG ogg)

########################################
# MistServer - Utilities               #
########################################
macro(makeUtil utilName utilFile)
  add_executable(MistUtil${utilName}
    src/utils/util_${utilFile}.cpp
    ${BINARY_DIR}/mist/.headers
  )
  target_link_libraries(MistUtil${utilName}
    mist
  )
  install(
    TARGETS MistUtil${utilName}
    DESTINATION bin
  )
endmacro()

makeUtil(RAX rax)
makeUtil(AMF amf)

########################################
# MistServer - Inputs                  #
########################################
macro(makeInput inputName format)
  add_executable(MistIn${inputName} 
    src/input/mist_in.cpp
    src/input/input.cpp 
    src/input/input_${format}.cpp 
    src/io.cpp
    ${BINARY_DIR}/mist/.headers
  )

  #Set compile definitions
  unset(my_definitions)
  list(APPEND my_definitions "INPUTTYPE=\"input_${format}.h\"")

  set_target_properties(MistIn${inputName} 
    PROPERTIES COMPILE_DEFINITIONS "${my_definitions}"
  )

  target_link_libraries(MistIn${inputName} 
    mist 
  )
  install(
    TARGETS MistIn${inputName} 
    DESTINATION bin
  )
endmacro()

makeInput(DTSC dtsc)
makeInput(MP3 mp3)
makeInput(FLV flv)
makeInput(OGG ogg)
makeInput(Buffer buffer)
makeInput(H264 h264)

########################################
# MistServer - Outputs                 #
########################################
macro(makeOutput outputName format)
  #Parse all extra arguments, for http and ts flags
  SET (tsBaseClass Output)
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput src/output/output_http.cpp)
    if (";${ARGN};" MATCHES ";ts;")
      SET(tsBaseClass HTTPOutput)
    endif()
  endif()
  if (";${ARGN};" MATCHES ";ts;")
    SET(tsOutput src/output/output_ts_base.cpp)
  endif()
  add_executable(MistOut${outputName}
    src/output/mist_out.cpp
    src/output/output.cpp
    src/output/output_${format}.cpp 
    src/io.cpp
    ${httpOutput}
    ${tsOutput} 
    ${BINARY_DIR}/mist/.headers
  )
  set_target_properties(MistOut${outputName} 
    PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_${format}.h\";TS_BASECLASS=${tsBaseClass}"
  )
  target_link_libraries(MistOut${outputName} 
    mist 
  )
  install( 
    TARGETS MistOut${outputName}
    DESTINATION bin
  )
endmacro()

makeOutput(RTMP rtmp)
makeOutput(OGG progressive_ogg http)
makeOutput(FLV progressive_flv http)
makeOutput(MP4 progressive_mp4 http)
makeOutput(MP3 progressive_mp3 http)
makeOutput(HSS hss             http)
makeOutput(HDS hds             http)
makeOutput(SRT srt             http)
makeOutput(JSON json           http)
makeOutput(TS ts                    ts)
makeOutput(HTTPTS httpts       http ts)
makeOutput(HLS hls             http ts)

add_executable(MistOutHTTP 
  src/output/mist_out.cpp
  src/output/output.cpp
  src/output/output_http.cpp 
  src/output/output_http_internal.cpp
  src/io.cpp
  generated/silverlight.js.h
  generated/embed.js.h
  generated/html5.js.h
  generated/flash_strobe.js.h
  generated/theoplayer.js.h
  generated/jwplayer.js.h
  generated/polytrope.js.h
  generated/dashjs.js.h
  generated/videojs.js.h
  generated/img.js.h
  generated/playerdash.js.h
  generated/playerdashlic.js.h
  generated/playervideo.js.h
  generated/playerhlsvideo.js.h
  generated/core.js.h
  generated/mist.css.h
)
set_target_properties(MistOutHTTP 
  PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_http_internal.h\""
)
target_link_libraries(MistOutHTTP mist)
install(
  TARGETS MistOutHTTP
  DESTINATION bin
)

########################################
# Documentation                        #
########################################
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${SOURCE_DIR}/Doxyfile.in ${BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target( docs
    ${DOXYGEN_EXECUTABLE} ${BINARY_DIR}/Doxyfile
    COMMAND rm -rf ${BINARY_DIR}/docs
    COMMAND mv ${SOURCE_DIR}/docs ${BINARY_DIR}
    WORKING_DIRECTORY ${SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif(DOXYGEN_FOUND)

########################################
# Sourcery                             #
########################################
#If cross compiling, assume failure.
if (CMAKE_CROSSCOMPILING)
  set(RUNA "1")
else()
  try_run(RUNA RUNB ${BINARY_DIR}/CMakeTmp ${SOURCE_DIR}/src/sourcery.cpp )
endif()

if("${RUNA}" EQUAL "42")
  message("Not cross compiling - building sourcery")
  add_executable(sourcery
    src/sourcery.cpp
  )
else()
  message("Detected cross compiling")
  if (EXISTS "${SOURCE_DIR}/sourcery")
    if (EXISTS "${BINARY_DIR}/sourcery")
      add_custom_target(sourcery )
    else()
      add_custom_target(sourcery 
        COMMAND cp ${SOURCE_DIR}/sourcery ${BINARY_DIR}/sourcery
      )
    endif()
  else()
    message(FATAL_ERROR "Cross compiling and no usable sourcery binary found - please supply it")
  endif()
endif()

########################################
# Embed Code                           #
########################################
add_custom_command(OUTPUT generated/silverlight.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/silverlight.js silverlight_js generated/silverlight.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/silverlight.js
)
add_custom_command(OUTPUT generated/embed.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/src/embed.js embed_js generated/embed.js.h
  DEPENDS sourcery ${SOURCE_DIR}/src/embed.js
)
add_custom_command(OUTPUT generated/html5.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/html5.js html5_js generated/html5.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/html5.js
)
add_custom_command(OUTPUT generated/flash_strobe.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/flash_strobe.js flash_strobe_js generated/flash_strobe.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/flash_strobe.js
)
add_custom_command(OUTPUT generated/theoplayer.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/theoplayer.js theoplayer_js generated/theoplayer.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/theoplayer.js
)
add_custom_command(OUTPUT generated/jwplayer.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/jwplayer.js jwplayer_js generated/jwplayer.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/jwplayer.js
)
add_custom_command(OUTPUT generated/polytrope.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/polytrope.js polytrope_js generated/polytrope.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/polytrope.js
)
add_custom_command(OUTPUT generated/dashjs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/dashjs.js dash_js generated/dashjs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/dashjs.js
)
add_custom_command(OUTPUT generated/videojs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/videojs.js video_js generated/videojs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/videojs.js
)
add_custom_command(OUTPUT generated/img.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/img.js img_js generated/img.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/img.js
)
add_custom_command(OUTPUT generated/playerdashlic.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/dash.js.license.js playerdashlic_js generated/playerdashlic.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/dash.js.license.js
)
add_custom_command(OUTPUT generated/playerdash.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/dash.all.min.js playerdash_js generated/playerdash.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/dash.all.min.js
)
add_custom_command(OUTPUT generated/playervideo.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/video.min.js playervideo_js generated/playervideo.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/video.min.js
)
add_custom_command(OUTPUT generated/playerhlsvideo.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/videojs-contrib-hls.min.js playerhlsvideo_js generated/playerhlsvideo.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/videojs-contrib-hls.min.js
)
add_custom_command(OUTPUT generated/core.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/core.js core_js generated/core.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/core.js
)
add_custom_command(OUTPUT generated/mist.css.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/mist.css mist_css generated/mist.css.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/mist.css
)

########################################
# Local Settings Page                  #
########################################
set(lspSOURCES
  ${SOURCE_DIR}/lsp/plugins/jquery.js
  ${SOURCE_DIR}/lsp/plugins/jquery.flot.min.js
  ${SOURCE_DIR}/lsp/plugins/jquery.flot.time.min.js
  ${SOURCE_DIR}/lsp/plugins/jquery.qrcode.min.js
  ${SOURCE_DIR}/lsp/minified.js
)

add_custom_command(OUTPUT ${BINARY_DIR}/generated/server.html 
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR="${SOURCE_DIR}" -DlspSOURCES="${lspSOURCES}" -P ${SOURCE_DIR}/CMakeServHtml.txt
  DEPENDS ${lspSOURCES} ${SOURCE_DIR}/CMakeServHtml.txt ${SOURCE_DIR}/lsp/main.css ${SOURCE_DIR}/lsp/header.html ${SOURCE_DIR}/lsp/footer.html
)
add_custom_command(OUTPUT generated/server.html.h
  COMMAND ./sourcery generated/server.html server_html generated/server.html.h
  DEPENDS sourcery ${BINARY_DIR}/generated/server.html
)

########################################
# MistController - Build               #
########################################
add_executable(MistController
  src/controller/controller_api.h
  src/controller/controller_statistics.h
  src/controller/controller_connectors.h
  src/controller/controller_storage.h
  src/controller/controller_capabilities.h
  src/controller/controller_streams.h
  src/controller/controller.cpp
  src/controller/controller_streams.cpp
  src/controller/controller_storage.cpp
  src/controller/controller_connectors.cpp
  src/controller/controller_statistics.cpp
  src/controller/controller_capabilities.cpp
  src/controller/controller_api.cpp
  generated/server.html.h
  ${BINARY_DIR}/mist/.headers
)
set_target_properties(MistController
  PROPERTIES COMPILE_DEFINITIONS RELEASE=${RELEASE}
)
target_link_libraries(MistController
  mist
)
install(
  TARGETS MistController
  DESTINATION bin
)

########################################
# Make Clean                           #
########################################
add_custom_target(clean-all
  COMMAND ${CMAKE_BUILD_TOOL} clean
  COMMAND rm -rf ${BINARY_DIR}/CMakeCache.txt
  COMMAND rm -rf ${BINARY_DIR}/cmake_install.cmake
  COMMAND rm -rf ${BINARY_DIR}/Makefile
  COMMAND rm -rf ${BINARY_DIR}/CMakeFiles
  COMMAND rm -rf ${BINARY_DIR}/Testing
  COMMAND rm -rf ${BINARY_DIR}/CTestTestfile.cmake
  COMMAND rm -rf ${BINARY_DIR}/DartConfiguration.tcl
  COMMAND rm -rf ${BINARY_DIR}/mist
)


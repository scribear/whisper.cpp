#
# libstream
#

set(TARGET libstream)

add_executable(${TARGET}
    emscripten.cpp
    )

include(DefaultTargetOptions)

target_link_libraries(${TARGET} PRIVATE
    whisper
    )

unset(EXTRA_FLAGS)

if (WHISPER_WASM_SINGLE_FILE)
    set(EXTRA_FLAGS "-s SINGLE_FILE=1")
    message(STATUS "Embedding WASM inside stream.js")

    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/bin/libstream.js
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/stream.wasm/stream.js
        )

    add_custom_command(
        TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/circular-audio-buffer.js
        ${CMAKE_CURRENT_SOURCE_DIR}/coi-serviceworker.js
        ${CMAKE_CURRENT_SOURCE_DIR}/raw-recorder.js
        ${CMAKE_CURRENT_SOURCE_DIR}/sipo-audio-buffer.js
        -t ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/stream.wasm/
        )
endif()

set_target_properties(${TARGET} PROPERTIES LINK_FLAGS " \
    -s MODULARIZE \
    -s EXPORT_NAME='makeWhisper' \
    -s EXPORT_ES6 \
    -s ENVIRONMENT=web,worker \
    --bind \
    -s USE_PTHREADS=1 \
    -s PTHREAD_POOL_SIZE=8 \
    -s INITIAL_MEMORY=1024MB \
    -s TOTAL_MEMORY=1024MB \
    -s FORCE_FILESYSTEM=1 \
    -s EXPORTED_RUNTIME_METHODS=\"['print', 'printErr', 'ccall', 'cwrap']\" \
    ${EXTRA_FLAGS} \
    ")

#
# stream.wasm
#

set(TARGET stream.wasm)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/index-tmpl.html  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET}/index.html @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../helpers.js    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET}/helpers.js @ONLY)

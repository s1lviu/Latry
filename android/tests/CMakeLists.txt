function(latry_add_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "unit")
endfunction()

latry_add_test(tst_audio_jitter_buffer
    tst_audio_jitter_buffer.cpp
    ${CMAKE_SOURCE_DIR}/AudioJitterBuffer.cpp
)

latry_add_test(tst_resampler
    tst_resampler.cpp
    ${CMAKE_SOURCE_DIR}/Resampler.cpp
)

latry_add_test(tst_audio_limiter
    tst_audio_limiter.cpp
    ${CMAKE_SOURCE_DIR}/AudioLimiter.cpp
)

latry_add_test(tst_app_launch_mode
    tst_app_launch_mode.cpp
)

latry_add_test(tst_android_audio_route_interop
    tst_android_audio_route_interop.cpp
    ${CMAKE_SOURCE_DIR}/AndroidAudioRouteInterop.cpp
)

add_executable(tst_audio_engine
    tst_audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngine.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngineRecording.cpp
    ${CMAKE_SOURCE_DIR}/AudioEnginePlayback.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngineFocus.cpp
    ${CMAKE_SOURCE_DIR}/AudioStreamDevice.cpp
    ${CMAKE_SOURCE_DIR}/AudioJitterBuffer.cpp
    ${CMAKE_SOURCE_DIR}/AudioLimiter.cpp
    ${CMAKE_SOURCE_DIR}/OpusWrapper.cpp
    ${CMAKE_SOURCE_DIR}/Resampler.cpp
    ${CMAKE_SOURCE_DIR}/AndroidAudioRecordInput.cpp
    ${CMAKE_SOURCE_DIR}/AndroidAudioTrackOutput.cpp
)
target_include_directories(tst_audio_engine PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_audio_engine PRIVATE Qt6::Core Qt6::Test Qt6::Multimedia ${OPUS_LIBRARY})
add_test(NAME tst_audio_engine COMMAND tst_audio_engine)
set_tests_properties(tst_audio_engine PROPERTIES LABELS "unit")

add_executable(tst_reflector_client
    tst_reflector_client.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClient.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClientConnection.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClientProtocol.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClientUdp.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClientPtt.cpp
    ${CMAKE_SOURCE_DIR}/ReflectorClientRecovery.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngine.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngineRecording.cpp
    ${CMAKE_SOURCE_DIR}/AudioEnginePlayback.cpp
    ${CMAKE_SOURCE_DIR}/AudioEngineFocus.cpp
    ${CMAKE_SOURCE_DIR}/AudioStreamDevice.cpp
    ${CMAKE_SOURCE_DIR}/AudioJitterBuffer.cpp
    ${CMAKE_SOURCE_DIR}/AudioLimiter.cpp
    ${CMAKE_SOURCE_DIR}/OpusWrapper.cpp
    ${CMAKE_SOURCE_DIR}/Resampler.cpp
    ${CMAKE_SOURCE_DIR}/AndroidAudioRecordInput.cpp
    ${CMAKE_SOURCE_DIR}/AndroidAudioTrackOutput.cpp
)
target_include_directories(tst_reflector_client PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_reflector_client PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Multimedia ${OPUS_LIBRARY})
target_compile_definitions(tst_reflector_client PRIVATE
    LATRY_VERSION_NAME="${LATRY_VERSION_NAME}"
)
add_test(NAME tst_reflector_client COMMAND tst_reflector_client)
set_tests_properties(tst_reflector_client PROPERTIES LABELS "unit")

add_executable(tst_reflector_live_integration
    tst_reflector_live_integration.cpp
)
target_include_directories(tst_reflector_live_integration PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_reflector_live_integration PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME tst_reflector_live_integration COMMAND tst_reflector_live_integration)
set_tests_properties(tst_reflector_live_integration PROPERTIES
    LABELS "integration"
    SKIP_RETURN_CODE 77
)

add_executable(tst_battery_optimization_handler
    tst_battery_optimization_handler.cpp
    ${CMAKE_SOURCE_DIR}/BatteryOptimizationHandler.cpp
)
target_include_directories(tst_battery_optimization_handler PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_battery_optimization_handler PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME tst_battery_optimization_handler COMMAND tst_battery_optimization_handler)
set_tests_properties(tst_battery_optimization_handler PROPERTIES LABELS "unit")

add_executable(tst_qml
    qml/quicktest_main.cpp
)
target_include_directories(tst_qml PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tst_qml PRIVATE Qt6::Core Qt6::Qml Qt6::Quick Qt6::QuickTest)
target_compile_definitions(tst_qml PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    LATRY_QML_IMPORT_ROOT="${CMAKE_SOURCE_DIR}"
)
add_test(NAME tst_qml COMMAND tst_qml)
set_tests_properties(tst_qml PROPERTIES
    LABELS "unit"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic"
)

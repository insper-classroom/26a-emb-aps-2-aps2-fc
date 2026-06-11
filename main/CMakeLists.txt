# ============================================================================
# main/CMakeLists.txt - Controle Pico 2 Wii Bowling (FreeRTOS SMP + EI)
# ============================================================================
# Fase 5: integra Edge Impulse (idle/shake) rodando no Core 1.
# ============================================================================

if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
    message("Skipping main build: FREERTOS_KERNEL_PATH not defined")
    return()
endif()

include(FreeRTOS_Kernel_import.cmake)

# --- Pastas do Edge Impulse ---
set(MODEL_FOLDER   ${CMAKE_CURRENT_LIST_DIR}/ei-model)
set(EI_SDK_FOLDER  ${MODEL_FOLDER}/edge-impulse-sdk)

# Helper RECURSIVE_FIND_FILE do proprio SDK da EI.
include(${EI_SDK_FOLDER}/cmake/utils.cmake)

# Defines exigidos pela EI (mesmos do edgeimpluse-runner que funciona).
add_definitions(-DEIDSP_QUANTIZE_FILTERBANK=0
                -DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1
                -DARM_MATH_LOOPUNROLL)

# Coleta recursiva de todas as fontes do SDK + modelo.
RECURSIVE_FIND_FILE(EI_SDK_CPP "${EI_SDK_FOLDER}" "*.cpp")
RECURSIVE_FIND_FILE(EI_SDK_CC  "${EI_SDK_FOLDER}" "*.cc")
RECURSIVE_FIND_FILE(EI_SDK_C   "${EI_SDK_FOLDER}" "*.c")
RECURSIVE_FIND_FILE(EI_SDK_S   "${EI_SDK_FOLDER}" "*.s")
RECURSIVE_FIND_FILE(EI_MODEL_CPP "${MODEL_FOLDER}/tflite-model" "*.cpp")

set(EI_SOURCES
    ${EI_SDK_CPP} ${EI_SDK_CC} ${EI_SDK_C} ${EI_SDK_S} ${EI_MODEL_CPP}
)

add_executable(pico_emb
    main.cpp
    sensors/mpu6050.cpp
    tasks/imu_task.cpp
    tasks/usb_task.cpp
    tasks/button_task.cpp
    tasks/motion_tx_task.cpp
    tasks/feedback_task.cpp
    tasks/ei_inference_task.cpp
    tasks/encoder_task.cpp
    usb/usb_descriptors.cpp
    audio/audio.cpp
    encoder/encoder.cpp
    ${EI_SOURCES}
)

target_include_directories(pico_emb PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/sensors
    ${CMAKE_CURRENT_LIST_DIR}/tasks
    ${CMAKE_CURRENT_LIST_DIR}/usb
    ${CMAKE_CURRENT_LIST_DIR}/audio
    ${CMAKE_CURRENT_LIST_DIR}/encoder
    # --- Edge Impulse includes ---
    ${MODEL_FOLDER}
    ${EI_SDK_FOLDER}
    ${EI_SDK_FOLDER}/third_party/ruy
    ${EI_SDK_FOLDER}/third_party/gemmlowp
    ${EI_SDK_FOLDER}/third_party/flatbuffers/include
    ${EI_SDK_FOLDER}/third_party
    ${EI_SDK_FOLDER}/tensorflow
    ${EI_SDK_FOLDER}/dsp
    ${EI_SDK_FOLDER}/classifier
    ${EI_SDK_FOLDER}/anomaly
    ${EI_SDK_FOLDER}/CMSIS/NN/Include
    ${EI_SDK_FOLDER}/CMSIS/DSP/PrivateInclude
    ${EI_SDK_FOLDER}/CMSIS/DSP/Include
    ${EI_SDK_FOLDER}/CMSIS/Core/Include
    ${EI_SDK_FOLDER}/porting
)

target_link_libraries(pico_emb PRIVATE
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_async_context_freertos
    FreeRTOS-Kernel-Heap4
    hardware_uart
    hardware_gpio
    hardware_i2c
    hardware_pwm
    tinyusb_device
    tinyusb_board
)

# SMP: 2 cores do RP2350.
target_compile_definitions(pico_emb PRIVATE
    configNUMBER_OF_CORES=2
)

# stdout no UART0 (GP0/GP1). USB e usada pelo TinyUSB (HID + CDC).
pico_enable_stdio_uart(pico_emb 1)
pico_enable_stdio_usb(pico_emb 0)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_emb PRIVATE pico_cyw43_arch_none)
endif()

pico_add_extra_outputs(pico_emb)

set_target_properties(pico_emb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

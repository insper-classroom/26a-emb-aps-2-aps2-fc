# ============================================================================
# main/CMakeLists.txt - Controle Pico 2 Wii Bowling (FreeRTOS SMP + EI)
# ============================================================================
# Fase 2: adiciona TinyUSB (HID Gamepad + CDC) e tasks usb_task/button_task.
# ============================================================================

if (NOT FREERTOS_KERNEL_PATH AND NOT DEFINED ENV{FREERTOS_KERNEL_PATH})
    message("Skipping main build: FREERTOS_KERNEL_PATH not defined")
    return()
endif()

include(FreeRTOS_Kernel_import.cmake)

add_executable(pico_emb
    main.cpp
    sensors/mpu6050.cpp
    tasks/imu_task.cpp
    tasks/usb_task.cpp
    tasks/button_task.cpp
    tasks/motion_tx_task.cpp
    tasks/feedback_task.cpp
    usb/usb_descriptors.cpp
)

target_include_directories(pico_emb PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/sensors
    ${CMAKE_CURRENT_LIST_DIR}/tasks
    ${CMAKE_CURRENT_LIST_DIR}/usb  # tusb_config.h fica aqui
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

target_compile_definitions(pico_emb PRIVATE
    configNUMBER_OF_CORES=2
)

# stdout no UART0 (GP0/GP1). USB e usada pelo TinyUSB (HID + CDC), NAO por stdio.
pico_enable_stdio_uart(pico_emb 1)
pico_enable_stdio_usb(pico_emb 0)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_emb PRIVATE pico_cyw43_arch_none)
endif()

pico_add_extra_outputs(pico_emb)

set_target_properties(pico_emb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

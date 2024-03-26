# config to select component, the format is CONFIG_USE_${component}
# Please refer to cmake files below to get available components:
#  ${SdkRootDirPath}/devices/MIMXRT685S/all_lib_device.cmake

set(CONFIG_COMPILER gcc)
set(CONFIG_TOOLCHAIN armgcc)
set(CONFIG_USE_COMPONENT_CONFIGURATION false)
set(CONFIG_USE_utility_debug_console_lite true)
set(CONFIG_USE_utility_assert_lite true)
set(CONFIG_USE_driver_common true)
set(CONFIG_USE_driver_clock true)
set(CONFIG_USE_driver_power true)
set(CONFIG_USE_driver_reset true)
set(CONFIG_USE_device_MIMXRT685S_CMSIS true)
set(CONFIG_USE_component_usart_adapter true)
set(CONFIG_USE_driver_flash_config_evkmimxrt685 true)
set(CONFIG_USE_driver_flexspi true)
set(CONFIG_USE_driver_cache_cache64 true)
set(CONFIG_USE_driver_flexcomm_usart true)
set(CONFIG_USE_component_lists true)
set(CONFIG_USE_device_MIMXRT685S_startup true)
set(CONFIG_USE_driver_flexcomm true)
set(CONFIG_USE_driver_lpc_iopctl true)
set(CONFIG_USE_driver_lpc_gpio true)
set(CONFIG_USE_utilities_misc_utilities true)
set(CONFIG_USE_CMSIS_Include_core_cm true)
set(CONFIG_CORE cm33)
set(CONFIG_DEVICE MIMXRT685S)
set(CONFIG_BOARD evkmimxrt685)
set(CONFIG_KIT evkmimxrt685)
set(CONFIG_DEVICE_ID MIMXRT685S)
set(CONFIG_FPU SP_FPU)
set(CONFIG_DSP DSP)
set(CONFIG_CORE_ID cm33)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Explicit directory, not a host-wide PATH replacement. The complete matched
# toolchain (including specs, multilib and runtime) is recorded after linking.
set(ARTOO_TOOLCHAIN_BIN "/usr/bin" CACHE PATH "Matched Arm compiler tools directory")
set(CMAKE_C_COMPILER "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-g++")
set(CMAKE_OBJCOPY "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-objcopy")
set(CMAKE_OBJDUMP "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-objdump")
set(CMAKE_AR "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-ar")
set(CMAKE_RANLIB "${ARTOO_TOOLCHAIN_BIN}/arm-none-eabi-ranlib")

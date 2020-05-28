#! /bin/bash

export NAOMI_BASE=/opt/toolchains/naomi
export NAOMI_SH_BASE=${NAOMI_BASE}/sh-elf
export NAOMI_ARM_BASE=${NAOMI_BASE}/arm-eabi
export NAOMI_SH_GCC_VER=9.3.0
export NAOMI_ARM_GCC_VER=8.4.0

export NAOMI_SH_CC=${NAOMI_SH_BASE}/bin/sh-elf-gcc
export NAOMI_SH_CPP=${NAOMI_SH_BASE}/bin/sh-elf-g++
export NAOMI_SH_LD=${NAOMI_SH_BASE}/bin/sh-elf-ld
export NAOMI_SH_AS=${NAOMI_SH_BASE}/bin/sh-elf-as
export NAOMI_SH_AR=${NAOMI_SH_BASE}/bin/sh-elf-ar
export NAOMI_SH_OBJCOPY=${NAOMI_SH_BASE}/bin/sh-elf-objcopy
export NAOMI_SH_OBJDUMP=${NAOMI_SH_BASE}/bin/sh-elf-objdump

export NAOMI_ARM_CC=${NAOMI_ARM_BASE}/bin/arm-eabi-gcc
export NAOMI_ARM_LD=${NAOMI_ARM_BASE}/bin/arm-eabi-ld
export NAOMI_ARM_AS=${NAOMI_ARM_BASE}/bin/arm-eabi-as
export NAOMI_ARM_AR=${NAOMI_ARM_BASE}/bin/arm-eabi-ar
export NAOMI_ARM_OBJCOPY=${NAOMI_ARM_BASE}/bin/arm-eabi-objcopy
export NAOMI_ARM_OBJDUMP=${NAOMI_ARM_BASE}/bin/arm-eabi-objdump

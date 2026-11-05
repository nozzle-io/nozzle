# Core source lists for Makefile-based wrappers. Set NOZZLE_DIR, then include this file.
#
#   NOZZLE_DIR := deps/nozzle
#   include $(NOZZLE_DIR)/sources/sources.mk
#
# An empty list must stop the build: a -shared link of nothing would still succeed.

ifndef NOZZLE_DIR
$(error NOZZLE_DIR must be set before including sources.mk)
endif

nozzle_source_group = $(addprefix $(NOZZLE_DIR)/,$(shell cat '$(NOZZLE_DIR)/sources/$(1).txt'))

NOZZLE_COMMON_SRCS := $(call nozzle_source_group,common)
NOZZLE_MACOS_SRCS := $(call nozzle_source_group,macos)
NOZZLE_WINDOWS_SRCS := $(call nozzle_source_group,windows)
NOZZLE_LINUX_SRCS := $(call nozzle_source_group,linux)
NOZZLE_OPENGL_SRCS := $(call nozzle_source_group,opengl)

ifeq ($(strip $(NOZZLE_COMMON_SRCS)),)
$(error could not read $(NOZZLE_DIR)/sources/common.txt)
endif
ifeq ($(strip $(NOZZLE_MACOS_SRCS)),)
$(error could not read $(NOZZLE_DIR)/sources/macos.txt)
endif
ifeq ($(strip $(NOZZLE_WINDOWS_SRCS)),)
$(error could not read $(NOZZLE_DIR)/sources/windows.txt)
endif
ifeq ($(strip $(NOZZLE_LINUX_SRCS)),)
$(error could not read $(NOZZLE_DIR)/sources/linux.txt)
endif
ifeq ($(strip $(NOZZLE_OPENGL_SRCS)),)
$(error could not read $(NOZZLE_DIR)/sources/opengl.txt)
endif

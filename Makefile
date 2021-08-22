#
# Makefile
#
# Makefile for AxisCameraUpload
#

include $(POCO_BASE)/build/rules/global

objects = AxisCameraUpload

target         = AxisCameraUpload
target_version = 1
target_libs    = PocoUtil PocoJSON PocoNet PocoXML PocoFoundation

include $(POCO_BASE)/build/rules/exec

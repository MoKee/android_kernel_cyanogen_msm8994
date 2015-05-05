ccflags-y += -Idrivers/media/platform/msm/camera_v2
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/io
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/cci
ifeq ($(CONFIG_MACH_PM9X),y)
obj-$(CONFIG_MSMB_CAMERA) += msm_actuator_pm9x.o
else
obj-$(CONFIG_MSMB_CAMERA) += msm_actuator.o
endif

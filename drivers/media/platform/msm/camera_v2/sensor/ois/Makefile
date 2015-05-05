ccflags-y += -Idrivers/media/platform/msm/camera_v2
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/io
ccflags-y += -Idrivers/media/platform/msm/camera_v2/sensor/cci
ifeq ($(CONFIG_MACH_PM9X),y)
obj-$(CONFIG_MSMB_CAMERA) += msm_ois_pm9x.o K7_Cmd.o K7_ois_init.o
else
obj-$(CONFIG_MSMB_CAMERA) += msm_ois.o
endif
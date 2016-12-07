deps_config := \
	/home/kq/kangqiao_seL4/RefOS_partition_all/tools/common/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/localB/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/localA/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/userC/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/userB/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/userA/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/hello_world4/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/hello_world3/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/hello_world2/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/hello_world1/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/hello_world/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/nethack/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/snake/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/tetris/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/test_user/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/test_os/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/terminal/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/timer_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/console_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/file_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/selfloader/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/apps/process_server/Kconfig \
	apps/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libapex/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libpartition/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libplatsupport/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libvterm/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libutils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/librefos/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/librefossys/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4utils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4vka/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4vspace/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4utils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4simple-stable/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4simple/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4platsupport/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4muslcsys/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4allocman/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libmuslc/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libelf/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libdatastruct/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libcpio/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/elfloader/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/libs/libsel4/Kconfig \
	libs/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/kernel/src/plat/pc99/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_partition_all/kernel/src/arch/arm/Kconfig \
	kernel/Kconfig \
	Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(SEL4_APPS_PATH)" "/home/kq/kangqiao_seL4/RefOS_partition_all/apps"
include/config/auto.conf: FORCE
endif
ifneq "$(SEL4_LIBS_PATH)" "/home/kq/kangqiao_seL4/RefOS_partition_all/libs"
include/config/auto.conf: FORCE
endif
ifneq "$(COMMON_PATH)" "/home/kq/kangqiao_seL4/RefOS_partition_all/tools/common"
include/config/auto.conf: FORCE
endif
ifneq "$(KERNEL_ROOT_PATH)" "/home/kq/kangqiao_seL4/RefOS_partition_all/kernel"
include/config/auto.conf: FORCE
endif

$(deps_config): ;

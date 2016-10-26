deps_config := \
	/home/kq/kangqiao_seL4/RefOS_x86/tools/common/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/localB/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/localA/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/userC/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/userB/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/userA/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/hello_world4/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/hello_world3/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/hello_world2/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/hello_world1/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/hello_world/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/nethack/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/snake/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/tetris/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/test_user/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/test_os/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/terminal/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/timer_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/console_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/file_server/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/selfloader/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/apps/process_server/Kconfig \
	apps/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libpartition/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libplatsupport/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libvterm/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libutils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/librefos/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/librefossys/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4utils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4vka/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4vspace/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4utils/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4simple-stable/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4simple/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4platsupport/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4muslcsys/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4allocman/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libmuslc/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libelf/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libdatastruct/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libcpio/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/elfloader/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/libs/libsel4/Kconfig \
	libs/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/kernel/src/plat/pc99/Kconfig \
	/home/kq/kangqiao_seL4/RefOS_x86/kernel/src/arch/arm/Kconfig \
	kernel/Kconfig \
	Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(SEL4_APPS_PATH)" "/home/kq/kangqiao_seL4/RefOS_x86/apps"
include/config/auto.conf: FORCE
endif
ifneq "$(SEL4_LIBS_PATH)" "/home/kq/kangqiao_seL4/RefOS_x86/libs"
include/config/auto.conf: FORCE
endif
ifneq "$(COMMON_PATH)" "/home/kq/kangqiao_seL4/RefOS_x86/tools/common"
include/config/auto.conf: FORCE
endif
ifneq "$(KERNEL_ROOT_PATH)" "/home/kq/kangqiao_seL4/RefOS_x86/kernel"
include/config/auto.conf: FORCE
endif

$(deps_config): ;

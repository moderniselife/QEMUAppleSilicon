arm_common_ss.add(when: 'CONFIG_APPLE_SOC', if_true: tasn1)
arm_common_ss.add(when: 'CONFIG_APPLE_DART', if_true: files('dart.c'),
                                             if_false: files('dart-stub.c'))
arm_common_ss.add(when: 'CONFIG_APPLE_SART', if_true: files('sart.c'))
arm_common_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files(
    'a13.c',
    'a13_gxf.c',
    'a9.c',
    'boot.c',
    'dtb.c',
    'mem.c',
    'mt-spi.c',
    'sep-sim.c',
    'sep.c',
    'xnu_kpf.c',
    'xnu_pf.c',
    'lm-backlight.c',
))
arm_ss.add(when: 'CONFIG_APPLE_SOC', if_true: files(
    's8000.c',
    't8030.c',
))

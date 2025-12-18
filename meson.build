project('imx-sdp', 'c',
    version: run_command(['git', 'describe', '--dirty', '--always'],
                         capture: true, check: true).stdout().strip(),
    license: 'GPL3',
)

libudev = dependency('libudev', required: get_option('udev'))
hidapi = dependency('hidapi-hidraw')
yaml = dependency('yaml-0.1')

src = files(
    'main.c',
    'sdp.c',
    'stages.c',
    'steps.c',
    'spec.c',
)

cfg = configuration_data()
cfg.set('VERSION', meson.project_version())

if libudev.found()
    cfg.set('WITH_UDEV', 1)
    src += 'udev.c'
endif

configure_file(input: 'config.h.in', output: 'config.h', configuration: cfg)
cfg_inc = include_directories('.')

executable('imx-sdp', src,
    dependencies: [libudev, hidapi, yaml],
    include_directories: cfg_inc,
)

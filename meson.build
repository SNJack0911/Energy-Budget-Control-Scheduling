project('sched_with_batsim', 'cpp',
  version: '0.1.0',
  license: 'LGPL-3.0',
  default_options: ['cpp_std=c++17'],
  meson_version: '>=0.40.0'
)

batprotocol_cpp_dep = dependency('batprotocol-cpp')
intervalset_dep = dependency('intervalset')
nlohmann_json_dep = dependency('nlohmann_json')
deps = [
  batprotocol_cpp_dep
, intervalset_dep
, nlohmann_json_dep
]

common = ['src/batsim_edc.h', 'src/energy_common.h']

easy = shared_library('easy', common + ['src/easy.cpp'],
  dependencies: deps,
  install: true,
)

pc_idle = shared_library('pc_idle', common + ['src/pc_idle.cpp'],
  dependencies: deps,
  install: true,
)

energybud_idle = shared_library('energybud_idle', common + ['src/energybud_idle.cpp'],
  dependencies: deps,
  install: true,
)

reducepc_idle = shared_library('reducepc_idle', common + ['src/reducepc_idle.cpp'],
  dependencies: deps,
  install: true,
)

pc_shutdown = shared_library('pc_shutdown', common + ['src/pc_shutdown.cpp'],
  dependencies: deps,
  install: true,
)

energybud_shutdown = shared_library('energybud_shutdown', common + ['src/energybud_shutdown.cpp'],
  dependencies: deps,
  install: true,
)

reducepc_shutdown = shared_library('reducepc_shutdown', common + ['src/reducepc_shutdown.cpp'],
  dependencies: deps,
  install: true,
)

project(
  'zestwm',
  'cpp',
  version: '6.8',
  meson_version: '>=1.3.0',
  default_options: [
    'cpp_std=gnu++23',
    'warning_level=1',
    'optimization=s',
  ],
)

cpp = meson.get_compiler('cpp')
strict_cpp = get_option('strict_cpp')
native_optimizations = get_option('native_optimizations')

add_project_arguments(
  cpp.get_supported_arguments('-Wno-deprecated-declarations'),
  language: 'cpp',
)

if strict_cpp
  add_project_arguments(
    cpp.get_supported_arguments(
      '-Wall',
      '-Wextra',
      '-Wpedantic',
      '-Wconversion',
      '-Wno-c++26-extensions',
      '-Werror',
    ),
    language: 'cpp',
  )
endif

if native_optimizations
  add_project_arguments(
    cpp.get_supported_arguments('-march=native'),
    language: 'cpp',
  )
endif

add_project_arguments(
  '-D_DEFAULT_SOURCE',
  '-D_BSD_SOURCE',
  '-D_XOPEN_SOURCE=700L',
  '-DVERSION="' + meson.project_version() + '"',
  language: 'cpp',
)

deps = [
  dependency('xcb'),
  dependency('xcb-randr'),
  dependency('xcb-cursor'),
  dependency('cairo-xcb'),
  dependency('pangocairo'),
  dependency('glib-2.0'),
  dependency('fontconfig'),
]

inc = include_directories('.', 'src')
integration_tests_enabled = get_option('integration_tests')
unit_tests_enabled = get_option('unit_tests')

zestwm_sources = files(
  'src/draw.cpp',
  'src/draw/bar.cpp',
  'src/client/client_lifecycle.cpp',
  'src/client/group_focus.cpp',
  'src/client/client_focus.cpp',
  'src/client/client_props.cpp',
  'src/client/client_transfer.cpp',
  'src/x11/color_utils.cpp',
  'src/x11/cursor_utils.cpp',
  'src/x11/monitor_query.cpp',
  'src/zestwm.cpp',
  'src/x11/wm_ops_window.cpp',
  'src/x11/wm_ops_pointer.cpp',
  'src/x11/wm_ops_server.cpp',
  'src/x11/wm_ops_input.cpp',
  'src/x11/wm_ops_property.cpp',
  'src/wm_state.cpp',
  'src/state/wm_session.cpp',
  'src/wm_startup.cpp',
  'src/actions.cpp',
  'src/actions/boundary_dispatch.cpp',
  'src/actions/boundary_entrypoints.cpp',
  'src/actions/workspace.cpp',
  'src/actions/workspace_boundary_dispatch.cpp',
  'src/actions/workspace_boundary_entrypoints.cpp',
  'src/actions/focus_cycle.cpp',
  'src/monitor/tree_arrange.cpp',
  'src/monitor/monitor_lifecycle.cpp',
  'src/geometry.cpp',
  'src/dispatch/runtime_dispatch.cpp',
  'src/dispatch/xcb_handlers.cpp',
  'src/dispatch/runtime_dispatch_xcb_event.cpp',
  'src/dispatch/runtime_dispatch_commands.cpp',
  'src/layout_tree.cpp',
  'src/bsp/workspace_store.cpp',
  'src/bsp/tree_ops.cpp',
  'src/bsp/tree_serialize.cpp',
  'src/bsp/tree_serialize_bind.cpp',
  'src/bsp/add_flow.cpp',
  'src/bsp/root_handoff.cpp',
  'src/bsp/remove_flow.cpp',
  'src/bsp/group_flow.cpp',
  'src/layoutmsg.cpp',
  'src/log.cpp',
  'src/autostart.cpp',
  'src/util.cpp',
  'src/sys/spawn.cpp',
  'src/workspace_registry.cpp',
  'src/workspace_selector.cpp',
  'src/special_workspace_registry.cpp',
  'src/x11/backend.cpp',
  'src/x11/xcb_props.cpp',
  'src/config/runtime.cpp',
  'src/config/env.cpp',
  'src/config/lifecycle.cpp',
  'src/config/watch.cpp',
  'src/config/parse/expand.cpp',
  'src/config/parse/action.cpp',
  'src/config/parse/bindings.cpp',
  'src/config/parse/blocks.cpp',
  'src/config/parse/core.cpp',
  'src/config/parse/general.cpp',
  'src/config/parse/group.cpp',
  'src/config/parse/sections.cpp',
  'src/config/parse/utils.cpp',
  'src/config/parse/values.cpp',
  'src/monitor_select.cpp',
  'src/context/monitor_context.cpp',
  'src/monitor/world_state.cpp',
  'src/monitor/arrange_world.cpp',
  'src/config/parse/window_rule.cpp',
  'src/config/parse/window_rule_parse.cpp',
  'src/config/parse/workspace.cpp',
  'src/input_apply.cpp',
)

executable(
  'zestwm',
  zestwm_sources,
  include_directories: inc,
  dependencies: deps,
  install: true,
)

executable(
  'zestctl',
  [
    'src/zestctl/main.cpp',
    'src/zestctl/helpers.cpp',
    'src/zestctl/query.cpp',
    'src/zestctl/query/internal.cpp',
    'src/zestctl/query/clients.cpp',
    'src/zestctl/query/general.cpp',
    'src/zestctl/query/sections.cpp',
    'src/zestctl/query/workspaces.cpp',
    'src/zestctl/x11.cpp',
    'src/x11/xcb_props.cpp',
    'src/config/parse/utils.cpp',
    'src/layoutmsg.cpp',
  ],
  include_directories: inc,
  dependencies: deps,
  install: true,
)

if unit_tests_enabled
  zestctl_split_ws_unit_test = executable(
    'zestctl-split-ws-unit-test',
    [
      'tests/unit/zestctl_split_ws_test.cpp',
      'src/zestctl/helpers.cpp',
      'src/config/parse/utils.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('zestctl-split-ws', zestctl_split_ws_unit_test)

  config_parse_unit_test = executable(
    'config-parse-unit-test',
    [
      'tests/unit/config_parse_test.cpp',
      'src/config/parse/utils.cpp',
      'src/config/parse/expand.cpp',
      'src/config/parse/values.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('config-parse', config_parse_unit_test)

  config_sections_unit_test = executable(
    'config-sections-unit-test',
    [
      'tests/unit/config_sections_test.cpp',
      'src/config/parse/sections.cpp',
      'src/config/parse/expand.cpp',
      'src/config/parse/utils.cpp',
      'src/config/parse/values.cpp',
      'src/log.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('config-sections', config_sections_unit_test)

  config_windowrule_unit_test = executable(
    'config-windowrule-unit-test',
    [
      'tests/unit/config_windowrule_test.cpp',
      'src/config/parse/window_rule.cpp',
      'src/config/parse/window_rule_parse.cpp',
      'src/config/parse/expand.cpp',
      'src/config/parse/utils.cpp',
      'src/config/parse/values.cpp',
      'src/log.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('config-windowrule', config_windowrule_unit_test)

  config_workspace_unit_test = executable(
    'config-workspace-unit-test',
    [
      'tests/unit/config_workspace_test.cpp',
      'tests/unit/config_workspace_stubs.cpp',
      'src/config/parse/workspace.cpp',
      'src/config/parse/expand.cpp',
      'src/config/parse/utils.cpp',
      'src/config/parse/values.cpp',
      'src/workspace_registry.cpp',
      'src/workspace_selector.cpp',
      'src/special_workspace_registry.cpp',
      'src/log.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('config-workspace', config_workspace_unit_test)

  config_env_unit_test = executable(
    'config-env-unit-test',
    [
      'tests/unit/config_env_test.cpp',
      'src/config/env.cpp',
      'src/config/parse/expand.cpp',
      'src/config/parse/utils.cpp',
      'src/sys/spawn.cpp',
      'src/log.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('config-env', config_env_unit_test)

  wm_state_root_unit_test = executable(
    'wm-state-root-unit-test',
    ['tests/unit/wm_state_root_test.cpp'],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  test('wm-state-root', wm_state_root_unit_test)

  monitor_persist_key_unit_test = executable(
    'monitor-persist-key-unit-test',
    ['tests/unit/monitor_persist_key_test.cpp'],
    include_directories: inc,
    install: false,
  )

  test('monitor-persist-key', monitor_persist_key_unit_test)

  group_focus_policy_unit_test = executable(
    'group-focus-policy-unit-test',
    ['tests/unit/group_focus_policy_test.cpp'],
    include_directories: inc,
    install: false,
  )

  test('group-focus-policy', group_focus_policy_unit_test)

  tree_serialize_unit_test = executable(
    'tree-serialize-unit-test',
    [
      'tests/unit/tree_serialize_test.cpp',
      'src/bsp/tree_serialize.cpp',
    ],
    include_directories: inc,
    dependencies: [dependency('xcb')],
    install: false,
  )

  test('tree-serialize', tree_serialize_unit_test)

  runtime_dispatch_xcb_event_unit_test = executable(
    'runtime-dispatch-xcb-event-unit-test',
    [
      'tests/unit/runtime_dispatch_xcb_event_test.cpp',
      'src/dispatch/runtime_dispatch_xcb_event.cpp',
    ],
    include_directories: inc,
    dependencies: [dependency('xcb')],
    install: false,
  )

  test('runtime-dispatch-xcb-event', runtime_dispatch_xcb_event_unit_test)
endif

if integration_tests_enabled
  xephyr_suite_runner = files('tests/integration/run_xephyr_suite.sh')

  # Isolate integration probe processes from the host session when run via
  # `meson test` directly. The suite wrapper (run_xephyr_suite.sh) already does
  # this, but `meson test` bypasses it and would otherwise inherit the caller's
  # real HOME/XDG_*, causing the test WM to write into ~/.local/state/zestwm and
  # touch the host dbus session. Each probe still allocates its own nested
  # :DISPLAY via xserver_common.inc.sh; this env only sandboxes filesystem state.
  # Capture host DISPLAY (empty if unset) so the probe guard rejects it as target.
  host_display_res = run_command('printenv', 'DISPLAY', check: false)
  probe_test_env = environment()
  probe_test_env.set('HOME', '/tmp/zestwm-meson-test-home')
  probe_test_env.set('XDG_CONFIG_HOME', '/tmp/zestwm-meson-test-xdg-config')
  probe_test_env.set('XDG_STATE_HOME', '/tmp/zestwm-meson-test-xdg-state')
  probe_test_env.set('XDG_CACHE_HOME', '/tmp/zestwm-meson-test-xdg-cache')
  probe_test_env.set('XDG_DATA_HOME', '/tmp/zestwm-meson-test-xdg-data')
  probe_test_env.set('ZESTWM_HOST_DISPLAY', host_display_res.stdout().strip())
  probe_test_env.set('ZESTWM_NESTED_X11_DEBUG', '0')
  probe_test_env.unset('XAUTHORITY')

  wmclass_property_test = executable(
    'wmclass-property-test',
    [
      'tests/integration/wmclass_property_probe.cpp',
      'src/x11/wm_ops_property.cpp',
      'src/x11/backend.cpp',
      'src/x11/xcb_props.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )

  wmclass_property_probe_script = files('tests/integration/wmclass_property_probe.sh')
  test(
    'wmclass-property',
    find_program('bash'),
    args: [wmclass_property_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  executable(
    'focusurgent-urgent-client-test',
    ['tests/integration/focusurgent_urgent_client.cpp'],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )
  executable(
    'set-utf8-root-property-test',
    [
      'tests/integration/set_utf8_root_property.cpp',
      'src/x11/xcb_props.cpp',
    ],
    include_directories: inc,
    dependencies: deps,
    install: false,
  )
  focusurgent_probe_script = files('tests/integration/focusurgent_probe.sh')
  reload_workspace_restore_probe_script = files('tests/integration/reload_workspace_restore_probe.sh')
  reload_skip_autostart_probe_script = files('tests/integration/reload_skip_autostart_probe.sh')
  reload_floating_geometry_probe_script = files('tests/integration/reload_floating_geometry_probe.sh')
  reload_special_workspace_probe_script = files('tests/integration/reload_special_workspace_probe.sh')
  reload_special_overlay_state_probe_script = files('tests/integration/reload_special_overlay_state_probe.sh')
  reload_groupmode_special_ungrouped_probe_script = files('tests/integration/reload_groupmode_special_ungrouped_probe.sh')
  reload_special_workspace_dropped_tag_probe_script = files('tests/integration/reload_special_workspace_dropped_tag_probe.sh')
  reload_multi_workspace_probe_script = files('tests/integration/reload_multi_workspace_probe.sh')
  reload_multi_special_workspace_probe_script = files('tests/integration/reload_multi_special_workspace_probe.sh')
  dispatch_special_aliases_probe_script = files('tests/integration/dispatch_special_aliases_probe.sh')
  parser_workspace_name_dispatch_probe_script = files('tests/integration/parser_workspace_name_dispatch_probe.sh')
  reload_persistence_conflict_probe_script = files('tests/integration/reload_persistence_conflict_probe.sh')
  reload_persistence_conflict_special_vs_tree_probe_script = files('tests/integration/reload_persistence_conflict_special_vs_tree_probe.sh')
  reload_persistence_special_hidden_id_probe_script = files('tests/integration/reload_persistence_special_hidden_id_probe.sh')
  group_single_client_probe_script = files('tests/integration/group_single_client_probe.sh')
  group_single_special_toggle_crash_probe_script = files('tests/integration/group_single_special_toggle_crash_probe.sh')
  group_special_workspace_regression_probe_script = files('tests/integration/group_special_workspace_regression_probe.sh')
  special_group_toggle_focus_reload_probe_script = files('tests/integration/special_group_toggle_focus_reload_probe.sh')
  close_second_client_crash_probe_script = files('tests/integration/close_second_client_crash_probe.sh')
  ewmh_dock_gap_probe_script = files('tests/integration/ewmh_dock_gap_probe.sh')
  workspace_defaultname_export_probe_script = files('tests/integration/workspace_defaultname_export_probe.sh')
  workspace_sparse_registry_probe_script = files('tests/integration/workspace_sparse_registry_probe.sh')
  workspace_monitor_selector_probe_script = files('tests/integration/workspace_monitor_selector_probe.sh')
  workspace_policy_keys_probe_script = files('tests/integration/workspace_policy_keys_probe.sh')
  workspace_rules_runtime_probe_script = files('tests/integration/workspace_rules_runtime_probe.sh')
  workspace_selector_wt1_probe_script = files('tests/integration/workspace_selector_wt1_probe.sh')
  xdg_autostart_probe_script = files('tests/integration/xdg_autostart_probe.sh')
  windowrule_keyword_probe_script = files('tests/integration/windowrule_keyword_probe.sh')
  windowrule_anon_block_probe_script = files('tests/integration/windowrule_anon_block_probe.sh')
  windowrule_named_block_probe_script = files('tests/integration/windowrule_named_block_probe.sh')
  windowrule_monitor_selector_probe_script = files('tests/integration/windowrule_monitor_selector_probe.sh')
  windowrule_special_workspace_probe_script = files('tests/integration/windowrule_special_workspace_probe.sh')
  windowrule_workspace_occupancy_export_probe_script = files('tests/integration/windowrule_workspace_occupancy_export_probe.sh')
  windowrule_scratchpad_magic_silent_probe_script = files('tests/integration/windowrule_scratchpad_magic_silent_probe.sh')
  windowrule_special_empty_tag_probe_script = files('tests/integration/windowrule_special_empty_tag_probe.sh')
  special_overlay_dialog_routing_probe_script = files('tests/integration/special_overlay_dialog_routing_probe.sh')
  floating_dialog_onscreen_probe_script = files('tests/integration/floating_dialog_onscreen_probe.sh')
  clients_floating_export_probe_script = files('tests/integration/clients_floating_export_probe.sh')
  randr_hotplug_probe_script = files('tests/integration/randr_hotplug_probe.sh')
  test(
    'focusurgent',
    find_program('bash'),
    args: [focusurgent_probe_script, meson.current_build_dir(), 'urgent'],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'focusurgent-no-urgent',
    find_program('bash'),
    args: [focusurgent_probe_script, meson.current_build_dir(), 'no-urgent'],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-workspace-restore',
    find_program('bash'),
    args: [reload_workspace_restore_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-skip-autostart',
    find_program('bash'),
    args: [reload_skip_autostart_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-floating-geometry',
    find_program('bash'),
    args: [reload_floating_geometry_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-special-workspace',
    find_program('bash'),
    args: [reload_special_workspace_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-special-overlay-state',
    find_program('bash'),
    args: [reload_special_overlay_state_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-groupmode-special-ungrouped',
    find_program('bash'),
    args: [reload_groupmode_special_ungrouped_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-special-workspace-dropped-tag',
    find_program('bash'),
    args: [reload_special_workspace_dropped_tag_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-multi-workspace',
    find_program('bash'),
    args: [reload_multi_workspace_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-multi-special-workspace',
    find_program('bash'),
    args: [reload_multi_special_workspace_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'dispatch-special-aliases',
    find_program('bash'),
    args: [dispatch_special_aliases_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'parser-workspace-name-dispatch',
    find_program('bash'),
    args: [parser_workspace_name_dispatch_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-persistence-conflict',
    find_program('bash'),
    args: [reload_persistence_conflict_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-persistence-conflict-special-vs-tree',
    find_program('bash'),
    args: [reload_persistence_conflict_special_vs_tree_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'reload-persistence-special-hidden-id',
    find_program('bash'),
    args: [reload_persistence_special_hidden_id_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'group-single-client',
    find_program('bash'),
    args: [group_single_client_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'group-single-special-toggle-crash',
    find_program('bash'),
    args: [group_single_special_toggle_crash_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'group-special-workspace-regression',
    find_program('bash'),
    args: [group_special_workspace_regression_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'special-group-toggle-focus-reload',
    find_program('bash'),
    args: [special_group_toggle_focus_reload_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'close-second-client-crash',
    find_program('bash'),
    args: [close_second_client_crash_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'ewmh-dock-gap',
    find_program('bash'),
    args: [ewmh_dock_gap_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-defaultname-export',
    find_program('bash'),
    args: [workspace_defaultname_export_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-sparse-registry',
    find_program('bash'),
    args: [workspace_sparse_registry_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-monitor-selector',
    find_program('bash'),
    args: [workspace_monitor_selector_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-policy-keys',
    find_program('bash'),
    args: [workspace_policy_keys_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-rules-runtime',
    find_program('bash'),
    args: [workspace_rules_runtime_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'workspace-selector-wt1',
    find_program('bash'),
    args: [workspace_selector_wt1_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'xdg-autostart',
    find_program('bash'),
    args: [xdg_autostart_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-keyword',
    find_program('bash'),
    args: [windowrule_keyword_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-anon-block',
    find_program('bash'),
    args: [windowrule_anon_block_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-named-block',
    find_program('bash'),
    args: [windowrule_named_block_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-monitor-selector',
    find_program('bash'),
    args: [windowrule_monitor_selector_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-special-workspace',
    find_program('bash'),
    args: [windowrule_special_workspace_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'special-overlay-dialog-routing',
    find_program('bash'),
    args: [special_overlay_dialog_routing_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'floating-dialog-onscreen',
    find_program('bash'),
    args: [floating_dialog_onscreen_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'clients-floating-export',
    find_program('bash'),
    args: [clients_floating_export_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-workspace-occupancy-export',
    find_program('bash'),
    args: [windowrule_workspace_occupancy_export_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-special-empty-tag',
    find_program('bash'),
    args: [windowrule_special_empty_tag_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'windowrule-scratchpad-magic-silent',
    find_program('bash'),
    args: [windowrule_scratchpad_magic_silent_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  test(
    'randr-hotplug',
    find_program('bash'),
    args: [randr_hotplug_probe_script, meson.current_build_dir()],
    env: probe_test_env,
    is_parallel: false,
    timeout: 300,
  )
  run_target(
    'xephyr-suite',
    command: [
      find_program('bash'),
      xephyr_suite_runner,
      '--build-dir',
      meson.current_build_dir(),
      '--no-build',
    ],
  )
endif

cpp_format_check = files('scripts/check-cpp-format.sh')
clang_format = find_program('clang-format', required: false)
if clang_format.found()
  run_target(
    'cpp-format',
    command: [find_program('bash'), cpp_format_check],
  )
  run_target(
    'cpp-format-fix',
    command: [find_program('bash'), cpp_format_check, '--fix'],
  )
endif

cpp_tidy_check = files('scripts/check-cpp-tidy.sh')
clang_tidy = find_program('clang-tidy', required: false)
if clang_tidy.found()
  run_target(
    'cpp-tidy',
    command: [find_program('bash'), cpp_tidy_check],
    env: ['BUILD_DIR=' + meson.current_build_dir()],
  )
endif

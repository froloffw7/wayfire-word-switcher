project('switcher', 'cpp',
    version: '0.1.0',
    default_options: ['cpp_std=c++20', 'warning_level=2'])

wayfire = dependency('wayfire')
# wlroots = dependency(['wlroots-0.20', 'wlroots-0.19'])
wfconfig = dependency('wf-config')

shared_module('word-switcher',
    'word-switcher.cpp',
    dependencies: [wayfire, wfconfig],
    install: true,
    install_dir: wayfire.get_variable('plugindir'))

install_data('word-switcher.xml',
    install_dir: wayfire.get_variable('metadatadir'))

project('gst-myfilter', 'c')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])

plugin_c_args = ['-DHAVE_CONFIG_H']

plugin_sources = [
  'MyFilter/gstmyfilter.c'
  ]

gstpluginexample = library('gstmyfilter',
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : [gst_dep],
  install : true,
  install_dir : plugins_install_dir,
)

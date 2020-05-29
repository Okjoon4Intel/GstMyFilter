plugin_c_args = ['-DHAVE_CONFIG_H']

cdata = configuration_data()
cdata.set_quoted('PACKAGE_VERSION', gst_version)
cdata.set_quoted('PACKAGE', 'gst-template-plugin')
cdata.set_quoted('GST_LICENSE', 'LGPL')
cdata.set_quoted('GST_API_VERSION', api_version)
cdata.set_quoted('GST_PACKAGE_NAME', 'GStreamer template Plug-ins')
cdata.set_quoted('GST_PACKAGE_ORIGIN', 'https://gstreamer.freedesktop.org')
configure_file(output : 'config.h', configuration : cdata)

plugin_sources = [
  'gstavdemuxer.c',
  'gstiestsdemux.c'
  ]

gstiestsdemux_plugin = library('gstiestsdemux',
  plugin_sources,
  c_args: plugin_c_args,
  dependencies : libav_deps + [gst_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)

pkgconfig.generate(gstiestsdemux_plugin, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstiestsdemux_plugin]


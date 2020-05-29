project('gstmyfilter', 'c')

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

cc = meson.get_compiler('c')

gst_version = meson.project_version()

api_version = '1.0'

libavfilter_dep = dependency('libavfilter', version: '>= 7.16.100',
  fallback: ['FFmpeg', 'libavfilter_dep'])
libavformat_dep = dependency('libavformat', version: '>= 58.12.100',
  fallback: ['FFmpeg', 'libavformat_dep'])
libavcodec_dep = dependency('libavcodec', version: '>= 58.18.100',
  fallback: ['FFmpeg', 'libavcodec_dep'])
libavutil_dep = dependency('libavutil', version: '>= 56.14.100',
  fallback: ['FFmpeg', 'libavutil_dep'])

libav_deps = [libavfilter_dep, libavformat_dep, libavcodec_dep, libavutil_dep]

gst_dep = dependency('gstreamer-1.0',
    fallback : ['gstreamer', 'gst_dep'])

#gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
#  fallback : ['gstreamer', 'gst_base_dep'])
gstbase_dep = dependency('gstreamer-base-1.0',
  fallback : ['gstreamer', 'gst_base_dep'])

subdir('MyFilter')
subdir('IESTsDemux')
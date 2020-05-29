
#ifndef __GST_IESTSDEMUX_H__
#define __GST_IESTSDEMUX_H__

#include "gstavdemuxer.h"

#include <gst/gst.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <gst/base/gstflowcombiner.h>

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_IESTSDEMUX \
  (gst_iestsdemux_get_type())
#define GST_IESTSDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IESTSDEMUX,Gstiestsdemux))
#define GST_IESTSDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IESTSDEMUX,GstiestsdemuxClass))
#define GST_IS_IESTSDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IESTSDEMUX))
#define GST_IS_IESTSDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IESTSDEMUX))

#define MAX_STREAMS 5

#define TSDEMUX_SINK_STATIC_CAPS		GST_STATIC_CAPS("video/mpegts, " "systemstream = (boolean)true ")
#define TSDEMUX_SINK_MEDIA_TYPE			"mpegts"
#define TSDEMUX_SRC_VIDEO_MIME_TYPE		"video/x-h264"
#define TSDEMUX_SRC_AUDIO_MIME_TYPE		"audio/mpeg"
#define TSDEMUX_SRC_METADATA_MIME_TYPE	"application/x-id3"
#define TSDEMUX_TYPEFIND_NAME			"ies_mpegts"

typedef enum AVMediaType		   GstMediaType;
typedef struct _GstAVStream		   GstAVStream;
typedef struct _Gstiestsdemux      Gstiestsdemux;
typedef struct _GstiestsdemuxClass GstiestsdemuxClass;

struct _GstAVStream
{
	GstPad			*srcpad;

	GstMediaType	av_media_type;
	AVStream		*avstream;

	gboolean		has_discontinuity;

	GstClockTime	ts_last_pos;
	GstTagList		*tags;
};

struct _Gstiestsdemux
{
	// Parent class
	GstElement		element;

	GstPad			*sinkpad;

	GstTagList		*tags;

	GstFlowCombiner *flow_combiner;

	gboolean		is_opened;
	gboolean		is_sink_pullmode;

	// TODO: Revisit
	gboolean		have_group_id;
	guint			group_id;

	GstClockTime	start_time;
	GstClockTime	duration;

	GstSegment		segment;

	// LibAV Properties
	AVFormatContext	*av_format_context;

	GstAVStream		*av_streams[MAX_STREAMS];
	GstBufferedIOInfo *sink_buffio_info;
	GstTask			*push_task;
	GRecMutex		push_task_lock;

	gint	num_of_all_streams;
	gint	num_of_video_streams;
	gint	num_of_audio_streams;
	gint	num_of_metadata_streams;

	gint	active_video_stream_index;
	gint	active_audio_stream_index;
	gint	active_metadata_stream_index;

	gchar	*metadata_id3_prefix_buff;
	gint	metadata_id3_prefix_size;

	// General properties
	gboolean silent;
};

struct _GstiestsdemuxClass
{
	GstElementClass parent_class;

	AVInputFormat * av_in_format;

	GstPadTemplate *sink_template;
	GstPadTemplate *video_src_template;
	GstPadTemplate *audio_src_template;
	GstPadTemplate *metadata_src_template;
};

GType gst_iestsdemux_get_type(void);

G_END_DECLS

#endif /* __GST_IESTSDEMUX_H__ */

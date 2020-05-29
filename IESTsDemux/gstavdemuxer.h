#ifndef __GST_AVWRAPPERS_H__
#define __GST_AVWRAPPERS_H__

#include <gst/gst.h>
#include <libavformat/avformat.h>
#include <gst/base/gstadapter.h>

// Macros
#define GST_PRINT_AVERROR(errorcode) G_STMT_START {		\
	gchar err_msg[512];									\
	av_strerror(errorcode, err_msg, sizeof(err_msg));	\
	GST_ERROR(err_msg);									\
} G_STMT_END

typedef struct _GstBufferedIOInfo GstBufferedIOInfo;

struct _GstBufferedIOInfo
{
	GstPad		*target_pad;

	GstAdapter *gst_adapter;

	AVIOContext *io_context;

	GMutex		io_sync_mutex;

	GCond		io_sync_cond;

	guint64		io_read_offset;

	guint64		io_read_needed;
	
	gboolean	is_seekable;

	gboolean	is_pullmode;
	
	gboolean	is_eos;
};

//G_GNUC_INTERNAL void init_pes_parser(void);

void init_avdemux(void);

int av_bufferedio_open(GstBufferedIOInfo *device_info);

int av_bufferedio_close(AVIOContext * context);

/*
* Allocate a new GstBufferedIOInfo instance and initialize it
*/
static GstBufferedIOInfo* alloc_bufferedio_info(GstPad *pad)
{
	GstBufferedIOInfo *buffio_info = g_new0(GstBufferedIOInfo, 1);
	
	buffio_info->target_pad = pad;

	buffio_info->io_read_offset = 0;
	buffio_info->io_read_needed = 0;
	buffio_info->is_seekable = FALSE;
	buffio_info->is_eos = FALSE;

	g_mutex_init(&buffio_info->io_sync_mutex);
	g_cond_init(&buffio_info->io_sync_cond);

	buffio_info->gst_adapter = gst_adapter_new();

	return buffio_info;
}

/*
* De-allocate the memory of the GstBufferedIO instance
*/
static void free_bufferedio_info(GstBufferedIOInfo * buffio_info)
{
	g_mutex_clear(&buffio_info->io_sync_mutex);
	g_cond_clear(&buffio_info->io_sync_cond);

	gst_object_unref(&buffio_info->gst_adapter);

	g_free(buffio_info);
}

/*
* Find the AVInputFormat for MPEG TS
*/
static AVInputFormat * av_get_input_format(const gchar* name)
{
	return av_find_input_format(name);
}
// TODO: Rewrite
/*
 * Convert a timestamp from libav to GStreamer
 */
static inline guint64
convert_timestamp_from_av_to_gst(gint64 pts, AVRational base)
{
	guint64 out;

	if (pts == AV_NOPTS_VALUE) {
		out = GST_CLOCK_TIME_NONE;
	}
	else {
		AVRational bq = { 1, GST_SECOND };
		out = av_rescale_q(pts, base, bq);
	}

	return out;
}

/*
 * Convert a timestamp from GStreamer to libav
 */
static inline gint64
convert_timestamp_from_gst_to_av(guint64 time, AVRational base)
{
	gint64 out;

	if (!GST_CLOCK_TIME_IS_VALID(time) || base.num == 0) {
		out = AV_NOPTS_VALUE;
	}
	else {
		AVRational bq = { 1, GST_SECOND };
		out = av_rescale_q(time, bq, base);
	}

	return out;
}

#endif

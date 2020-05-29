#include "gstavdemuxer.h"

#include <string.h>
#include <errno.h>

GST_DEBUG_CATEGORY_STATIC(gst_avdemux_debug);
#define GST_CAT_DEFAULT gst_avdemux_debug

static int av_bufferedio_read_from_upstream(void *opaque, uint8_t *buf, int size);
static int av_bufferedio_read_from_adapter(void *opaque, uint8_t * buf, int size);
static int64_t av_bufferedio_seek(void *opaque, int64_t pos, int whence);

/*
* Start the buffered io operation. The read operation will different between push and pull mode.
*/
int 
av_bufferedio_open(GstBufferedIOInfo * buffio_info)
{
	int result = 0;
	static const int buffio_size = 4096;
	unsigned char *buffio_buffer = NULL;
	int flags = AVIO_FLAG_READ;

	GST_DEBUG("Start the buffered IO operation in libav");

	if (!buffio_info->is_pullmode) {
		// The Gst Adapter is required for the pull mode
		g_return_val_if_fail(GST_IS_ADAPTER(buffio_info->gst_adapter), AVERROR(EINVAL));
	}

	// Allocate the IO Buffer memory
	buffio_buffer = av_malloc(buffio_size);
	g_return_val_if_fail(buffio_buffer != NULL, AVERROR(ENOMEM));

	// Allocate the IO context
	if (buffio_info->is_pullmode) {
		buffio_info->io_context = avio_alloc_context(buffio_buffer, buffio_size, 0, (void *)buffio_info,
			av_bufferedio_read_from_upstream, NULL, av_bufferedio_seek);
	}
	else {
		buffio_info->io_context = avio_alloc_context(buffio_buffer, buffio_size, 0, (void *)buffio_info,
			av_bufferedio_read_from_adapter, NULL, NULL);
	}

	if (buffio_info->io_context == NULL) {
		GST_ERROR("Failed to allocate the io context!");
		result = AVERROR(EINVAL);
		goto ex_averror;
	}

	// Set the buffered IO properties
	if (buffio_info->is_pullmode) {
		buffio_info->io_context->seekable = AVIO_SEEKABLE_NORMAL;
		buffio_info->io_context->buf_ptr = buffio_info->io_context->buf_end;
	}
	else {
		buffio_info->io_context->seekable = 0;
		buffio_info->io_context->buf_ptr = buffio_info->io_context->buf_end;
	}

	goto fn_done;

ex_averror:
	if (buffio_buffer != NULL)
		g_free(buffio_buffer);

fn_done:
	return result;
}

/*
* Stop the buffered io operation
*/
int av_bufferedio_close(AVIOContext * context)
{
	GST_DEBUG("Close the buffered io operation");
	
	if (context == NULL)
		return 0;

	GstBufferedIOInfo* bufferio_info = (GstBufferedIOInfo *)context->opaque;
	if (bufferio_info == NULL)
		return 0;

	// Clear the io context
	context->opaque = NULL;
	av_freep(&context->buffer);
	av_free(context);

	return 0;
}

/*
* It is a read callback in the buffered IO operation. It pulls the data from the upstream and transfers to libav
*/
static int
av_bufferedio_read_from_upstream(void *opaque, uint8_t *buf, int size)
{
	GstBuffer *buff_read = NULL;
	gsize bytes_read = -1;

	GstBufferedIOInfo *buffio_info = (GstBufferedIOInfo *)opaque;
	g_assert_nonnull(buffio_info);

	// Pull a buffer from the peer pad
	GstFlowReturn ret = gst_pad_pull_range(buffio_info->target_pad, buffio_info->io_read_offset, (guint)size, &buff_read);
	if (ret == GST_FLOW_OK) {
		bytes_read = gst_buffer_get_size(buff_read);
		gst_buffer_extract(buff_read, 0, buf, bytes_read);
		gst_buffer_unref(buff_read);
	}
	else if (ret == GST_FLOW_EOS) {
		bytes_read = 0;
	}

	if (bytes_read >= 0) {
		buffio_info->io_read_offset += bytes_read;
	}

	GST_DEBUG("Read %d bytes and the total bytes read is %d", bytes_read, buffio_info->io_read_offset);

	return (int)bytes_read;
}

/*
* It is a read callback for the buffered IO operation. It transfers the data from the GST adapter to libav.
*/
static int
av_bufferedio_read_from_adapter(void *opaque, uint8_t * buf, int size)
{
	gsize bytes_available = 0;
	gsize bytes_read = 0;

	GstBufferedIOInfo * buffio_info = (GstBufferedIOInfo *)opaque;
	g_assert_nonnull(buffio_info);

	g_mutex_lock(&buffio_info->io_sync_mutex);

	// Wait until the requested size of data is available
	// The Chain function will notify that the data is available in the adapter
	while ((bytes_available = gst_adapter_available(buffio_info->gst_adapter)) < (gsize)size &&
		buffio_info->is_eos == FALSE) {

		buffio_info->io_read_needed = size;

		g_cond_signal(&buffio_info->io_sync_cond);
		g_cond_wait(&buffio_info->io_sync_cond, &buffio_info->io_sync_mutex);
	}

	// Copy media data from the adapter to the buffer which will be accessed by libav
	bytes_read = MIN(size, bytes_available);
	if (bytes_read) {
		gst_adapter_copy(buffio_info->gst_adapter, buf, 0, bytes_read);
		gst_adapter_flush(buffio_info->gst_adapter, bytes_read);
	}

	g_mutex_unlock(&buffio_info->io_sync_mutex);

	return (int)bytes_read;
}

/*
* It is a seek callback for the buffered IO operation.
*/
static int64_t
av_bufferedio_seek(void *opaque, int64_t pos, int whence)
{
	guint64 new_pos = 0;

	GstBufferedIOInfo *buffio_info = (GstBufferedIOInfo *)opaque;
	g_assert_nonnull(buffio_info);
	g_assert_true(buffio_info->is_pullmode);
	g_assert_true(GST_PAD_IS_SINK(buffio_info->target_pad));

	// Set the new position offseted from the whence
	switch (whence) {
	case SEEK_SET:
		new_pos = (guint64)pos;
		break;

	case SEEK_CUR:
		new_pos = buffio_info->io_read_offset + pos;
		break;

	case SEEK_END:
	case AVSEEK_SIZE:
	{
		gint64 duration;
		if (gst_pad_is_linked(buffio_info->target_pad) &&
			gst_pad_query_duration(GST_PAD_PEER(buffio_info->target_pad), GST_FORMAT_BYTES, &duration)) {
			new_pos = ((guint64)duration) + pos;
		}
		break;
	}

	default:
		g_assert_not_reached();
		break;
	}

	// According to the comment in avio.h, AVSEEK_SIZE should return the filesize without seeking anywhere.
	if (whence != AVSEEK_SIZE) {
		buffio_info->io_read_offset = new_pos;
	}

	GST_DEBUG("Seeking to %" G_GUINT64_FORMAT " (returning %" G_GUINT64_FORMAT ")", buffio_info->io_read_offset, new_pos);

	return new_pos;
}

/*
* Set the debug category
*/
void
init_avdemux(void)
{
	GST_DEBUG_CATEGORY_INIT(gst_avdemux_debug, "avdemux", 0, "LibAV Demuxer");
}

/**
 * SECTION:element-iestsdemux
 *
 * Demux the MPEG TS media format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m filesrc location=sample.ts ! iestsdemux ! avdec_h264 ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstiestsdemux.h"
#include "gstavdemuxer.h"

GST_DEBUG_CATEGORY_STATIC(gst_iestsdemux_debug);
#define GST_CAT_DEFAULT gst_iestsdemux_debug

/* Filter signals and args */
enum
{
	/* FILL ME */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, TSDEMUX_SINK_STATIC_CAPS);

// TODO: Define the video caps
static GstStaticPadTemplate video_src_factory =
GST_STATIC_PAD_TEMPLATE("video_%u", GST_PAD_SRC, GST_PAD_SOMETIMES, GST_STATIC_CAPS("ANY"));

// TODO: Define the video caps
static GstStaticPadTemplate audio_src_factory =
GST_STATIC_PAD_TEMPLATE("audio_%u", GST_PAD_SRC, GST_PAD_SOMETIMES, GST_STATIC_CAPS("ANY"));

static GstStaticPadTemplate metadata_src_factory =
GST_STATIC_PAD_TEMPLATE("metadata_%u", GST_PAD_SRC, GST_PAD_SOMETIMES, GST_STATIC_CAPS("application/x-id3"));

#define gst_iestsdemux_parent_class parent_class
G_DEFINE_TYPE(Gstiestsdemux, gst_iestsdemux, GST_TYPE_ELEMENT);

//-------------------------------------
// Element Callback Functions
//-------------------------------------
static void gst_iestsdemux_finalize(GObject * object);
static void gst_iestsdemux_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_iestsdemux_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_iestsdemux_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_iestsdemux_send_event(GstElement * element, GstEvent * event);
static void gst_iestsdemux_type_find(GstTypeFind * find, gpointer user_data);

//-------------------------------------
// Sink Pad Callback Functions
//-------------------------------------
static GstFlowReturn gst_iestsdemux_chain(GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean gst_iestsdemux_sink_event(GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean gst_iestsdemux_sink_query(GstPad * pad, GstObject * parent, GstQuery * query);
static gboolean gst_iestsdemux_sink_activate(GstPad * sinkpad, GstObject * parent);
static gboolean gst_iestsdemux_sink_activate_mode(GstPad * sinkpad, GstObject * parent, GstPadMode mode, gboolean active);

//-------------------------------------
// Source Pad Callback Functions
//-------------------------------------
static gboolean gst_iestsdemux_src_event(GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean gst_iestsdemux_src_query(GstPad * pad, GstObject * parent, GstQuery * query);

//-------------------------------------
// Private Functions
//-------------------------------------
static void gst_iestsdemux_loop(Gstiestsdemux * demux);
static gboolean gst_iestsdemux_sink_activate_pushmode(GstPad * sinkpad, GstObject * parent, gboolean active);
static gboolean gst_iestsdemux_sink_activate_pullmode(GstPad * sinkpad, GstObject * parent, gboolean active);
static gboolean gst_iestsdemux_push_event_to_srcpads(Gstiestsdemux * demux, GstEvent * event);
static gboolean gst_iestsdemux_do_seek(Gstiestsdemux * demux, GstEvent * event);
static void gst_iestsdemux_push_tags_to_srcpads(Gstiestsdemux * demux);

//-------------------------------------
// LibAV Supported Functions
//-------------------------------------
static gboolean av_streams_open(Gstiestsdemux * demux);
static void av_streams_close(Gstiestsdemux * demux);
static gboolean av_streams_seek(Gstiestsdemux * demux, GstSegment * segment);
static GstAVStream * av_streams_demux(Gstiestsdemux * demux, GstBuffer ** buff);
static gboolean av_streams_parse_stream(Gstiestsdemux * demux, AVStream * avstream, int index);
static void av_streams_parse_metadata_to_taglists(Gstiestsdemux * demux);

/*
 * Initialize the iestsdemux's class
 */
static void
gst_iestsdemux_class_init(GstiestsdemuxClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *)klass;
	gstelement_class = (GstElementClass *)klass;

	gstelement_class->change_state = gst_iestsdemux_change_state;

	gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_iestsdemux_finalize);
	gobject_class->set_property = gst_iestsdemux_set_property;
	gobject_class->get_property = gst_iestsdemux_get_property;

	g_object_class_install_property(gobject_class, PROP_SILENT,
		g_param_spec_boolean("silent", "Silent", "Produce verbose output ?", FALSE, G_PARAM_READWRITE));

	gst_element_class_set_metadata(gstelement_class,
		"MPEG traansport stream demuxer", "Demuxer",
		"Demux MPEG2 transport stream", "Intel Sports <<UNKNOWN-TODO@intel.com>>");

	klass->av_in_format = av_get_input_format(TSDEMUX_SINK_MEDIA_TYPE);
	klass->sink_template = gst_static_pad_template_get(&sink_factory);
	klass->video_src_template = gst_static_pad_template_get(&video_src_factory);
	klass->audio_src_template = gst_static_pad_template_get(&audio_src_factory);
	klass->metadata_src_template = gst_static_pad_template_get(&metadata_src_factory);
		
	gst_element_class_add_pad_template(gstelement_class, klass->sink_template);
	gst_element_class_add_pad_template(gstelement_class, klass->video_src_template);
	gst_element_class_add_pad_template(gstelement_class, klass->audio_src_template);
	gst_element_class_add_pad_template(gstelement_class, klass->metadata_src_template);
}

/*
 * Initialize the new element
 */
static void
gst_iestsdemux_init(Gstiestsdemux * demux)
{
	// Initialize the sink pad and assign the callback functions
	demux->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
	gst_pad_set_event_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_iestsdemux_sink_event));
	gst_pad_set_chain_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_iestsdemux_chain));
	gst_pad_set_activate_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_iestsdemux_sink_activate));
	gst_pad_set_activatemode_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_iestsdemux_sink_activate_mode));
	GST_PAD_SET_PROXY_CAPS(demux->sinkpad);
	gst_element_add_pad(GST_ELEMENT(demux), demux->sinkpad);

	// Initalize the data structture
	demux->is_opened = FALSE;
	demux->is_sink_pullmode = FALSE;
	for (int i = 0; i < MAX_STREAMS; i++) {
		demux->av_streams[i] = NULL;
	}
	demux->tags = NULL;
	demux->av_format_context = NULL;
	demux->num_of_all_streams = 0;
	demux->num_of_video_streams = 0;
	demux->num_of_audio_streams = 0;
	demux->num_of_metadata_streams = 0;
	demux->active_video_stream_index = -1;
	demux->active_audio_stream_index = -1;
	demux->active_metadata_stream_index = -1;
	demux->silent = FALSE;

	demux->metadata_id3_prefix_size = 5;
	demux->metadata_id3_prefix_buff = g_strdup_printf("ID3%c%c", 0x04, 0x00);

	// TODO: Revisit
	demux->have_group_id = FALSE;
	demux->group_id = G_MAXUINT;

	gst_segment_init(&demux->segment, GST_FORMAT_TIME);
	demux->flow_combiner = gst_flow_combiner_new();

	demux->push_task = gst_task_new((GstTaskFunction)gst_iestsdemux_loop, demux, NULL);
	g_rec_mutex_init(&demux->push_task_lock);
	gst_task_set_lock(demux->push_task, &demux->push_task_lock);

	demux->sink_buffio_info = alloc_bufferedio_info(demux->sinkpad);
}

/*
 * Finalize the element
 */
static void
gst_iestsdemux_finalize(GObject * object)
{
	Gstiestsdemux *demux = GST_IESTSDEMUX(object);

	gst_flow_combiner_free(demux->flow_combiner);

	gst_object_unref(demux->push_task);
	g_rec_mutex_clear(&demux->push_task_lock);

	free_bufferedio_info(demux->sink_buffio_info);

	g_free(demux->metadata_id3_prefix_buff);

	// Revisit later
	G_OBJECT_CLASS(gst_iestsdemux_parent_class)->finalize(object);
}

/*
 * Set properties for the element
 */
static void
gst_iestsdemux_set_property(GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	Gstiestsdemux *demux = GST_IESTSDEMUX(object);

	switch (prop_id) {
	case PROP_SILENT:
		demux->silent = g_value_get_boolean(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/*
 * Get properties for the element
 */
static void
gst_iestsdemux_get_property(GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	Gstiestsdemux *demux = GST_IESTSDEMUX(object);

	switch (prop_id) {
	case PROP_SILENT:
		g_value_set_boolean(value, demux->silent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

/*
 * Handle the events in the sink pad
 */
static gboolean
gst_iestsdemux_sink_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
	gboolean ret_val = TRUE;

	Gstiestsdemux *demux = GST_IESTSDEMUX(parent);
	g_assert_nonnull(demux);

	GstBufferedIOInfo *buffio_info = demux->sink_buffio_info;
	g_assert_nonnull(buffio_info);

	GST_DEBUG("The pad(%s) received %s event: %" GST_PTR_FORMAT, GST_PAD_NAME(pad), GST_EVENT_TYPE_NAME(event), event);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_STREAM_START:
	{
		gst_event_unref(event);
		break;
	}

	case GST_EVENT_CAPS:
	{
		GstCaps * caps;
		gst_event_parse_caps(event, &caps);

		// Forward the event
		ret_val = gst_pad_event_default(pad, parent, event);
		break;
	}

	case GST_EVENT_FLUSH_START:
	{
		// Forward the event
		ret_val = gst_pad_event_default(pad, parent, event);

		// TODO: Put here anything to be done
		//if (!sink_io_info->is_pullmode)
		//{
		//	g_mutex_lock(&sink_io_info->io_sync_mutex);

		//	g_mutex_unlock(&sink_io_info->io_sync_mutex);
		//}
		break;
	}

	case GST_EVENT_FLUSH_STOP:
	{
		// Forward the event
		ret_val = gst_pad_event_default(pad, parent, event);

		if (!buffio_info->is_pullmode)
		{
			g_mutex_lock(&buffio_info->io_sync_mutex);
			// Clear any queued data in the adapter
			gst_adapter_clear(buffio_info->gst_adapter);
			// TODO: Double-check it
			gst_task_start(demux->push_task);
			g_mutex_unlock(&buffio_info->io_sync_mutex);
		}
		break;
	}

	case GST_EVENT_EOS:
	{
		if (!buffio_info->is_pullmode)
		{
			g_mutex_lock(&buffio_info->io_sync_mutex);
			buffio_info->is_eos = TRUE;
			// Clear any queued data in the adapter
			gst_adapter_clear(buffio_info->gst_adapter);		// TODO: Double-check
			g_cond_signal(&buffio_info->io_sync_cond);
			g_mutex_unlock(&buffio_info->io_sync_mutex);
		}

		gst_event_unref(event);
		break;
	}

	default:
		ret_val = gst_pad_event_default(pad, parent, event);
		break;
	}
	return ret_val;
}

/*
 * Handle the events in the source pad
 */
static gboolean 
gst_iestsdemux_src_event(GstPad * pad, GstObject * parent, GstEvent * event)
{
	gboolean result = TRUE;
	Gstiestsdemux *demux = GST_IESTSDEMUX(parent);
	g_assert_nonnull(demux);

	GST_DEBUG("The pad(%s) received %s event: %" GST_PTR_FORMAT, GST_PAD_NAME(pad), GST_EVENT_TYPE_NAME(event), event);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_SEEK:
		result = gst_iestsdemux_do_seek(demux, event);
		gst_event_unref(event);
		break;

	case GST_EVENT_LATENCY:
		result = gst_pad_push_event(demux->sinkpad, event);
		break;

	case GST_EVENT_NAVIGATION:
	case GST_EVENT_QOS:
	default:
		result = FALSE;
		gst_event_unref(event);
		break;
	}

	return result;
}

/*
 * Handle the queries in the sink pad
 */
static gboolean 
gst_iestsdemux_sink_query(GstPad * pad, GstObject * parent, GstQuery * query)
{
	gboolean ret_val = TRUE;
	Gstiestsdemux *demux = GST_IESTSDEMUX(parent);

	GST_INFO_OBJECT(demux, "The pad(%s) received %s query.", GST_PAD_NAME(pad), GST_QUERY_TYPE_NAME(query));

	switch (GST_QUERY_TYPE(query)) {
		//case GST_QUERY_CAPS:
		//case GST_QUERY_ACCEPT_CAPS:
		//case GST_QUERY_ALLOCATION:

	default:
		/* just call the default handler */
		ret_val = gst_pad_query_default(pad, parent, query);
		break;
	}

	return ret_val;
}

/*
 * Handle the queries in the source pad
 */
gboolean gst_iestsdemux_src_query(GstPad * pad, GstObject * parent, GstQuery * query)
{
	gboolean result = TRUE;
	GstAVStream *gst_stream = NULL;
	AVStream *av_stream = NULL;

	Gstiestsdemux *demux = GST_IESTSDEMUX(parent);
	g_assert_nonnull(demux);

	gst_stream = gst_pad_get_element_private(pad);
	g_return_val_if_fail(gst_stream != NULL, FALSE);

	av_stream = gst_stream->avstream;

	GST_INFO_OBJECT(demux, "The pad(%s) received %s query.", GST_PAD_NAME(pad), GST_QUERY_TYPE_NAME(query));

	switch (GST_QUERY_TYPE(query)) {
		case GST_QUERY_POSITION:
		{
			GstFormat format;
			gint64 position;

			gst_query_parse_position(query, &format, NULL);

			position = gst_stream->ts_last_pos;
			if (!(GST_CLOCK_TIME_IS_VALID(position)))
				break;

			// Set the position query result int the given format
			switch (format) {
			case GST_FORMAT_TIME:
				gst_query_set_position(query, GST_FORMAT_TIME, position);
				result = TRUE;
				break;
				
			case GST_FORMAT_DEFAULT:
				gst_query_set_duration(query, GST_FORMAT_DEFAULT,
					gst_util_uint64_scale(position, av_stream->avg_frame_rate.num,
						GST_SECOND * av_stream->avg_frame_rate.den));
				result = TRUE;
				break;

			case GST_FORMAT_BYTES:
				// TODO: ???
				break;
			}
			break;
		}
		
		case GST_QUERY_DURATION:
		{
			GstFormat format;
			gint64 duration;

			gst_query_parse_duration(query, &format, NULL);

			duration = convert_timestamp_from_av_to_gst(av_stream->duration, av_stream->time_base);
			if (!(GST_CLOCK_TIME_IS_VALID(duration))) {
				duration = demux->duration;
				if (!(GST_CLOCK_TIME_IS_VALID(duration)))
					break;
			}

			// Set the duration query result in the given format
			switch (format) {
			case GST_FORMAT_TIME:
				gst_query_set_duration(query, GST_FORMAT_TIME, duration);
				result = TRUE;
				break;
			case GST_FORMAT_DEFAULT:
				gst_query_set_duration(query, GST_FORMAT_DEFAULT,
					gst_util_uint64_scale(duration, av_stream->avg_frame_rate.num,
						GST_SECOND * av_stream->avg_frame_rate.den));
				result = TRUE;
				break;
			case GST_FORMAT_BYTES:
				// TODO: ??
				break;
			}
			break;
		}
		
		case GST_QUERY_CAPS:
		{
			// TODO: we should report the supported caps here
			result = gst_pad_query_default(pad, parent, query);
			break;
		}

		case GST_QUERY_SEEKING:
		{
			GstFormat format;
			gboolean seekable;
			gint64 duration = -1;

			gst_query_parse_seeking(query, &format, NULL, NULL, NULL);
			seekable = demux->is_sink_pullmode;
			if (!gst_pad_query_duration(pad, format, &duration)) {
				seekable = FALSE;
				duration = -1;
			}

			// Set the seeking query result
			gst_query_set_seeking(query, format, seekable, 0, duration);
			result = TRUE;
			break;
		}

		case GST_QUERY_SEGMENT:
		{
			gdouble playback_rate;
			GstFormat format;
			gint64 start_pos, stop_pos;

			playback_rate = demux->segment.rate;
			format = demux->segment.format;

			// The normal playback segment of a pipeline is 0 to duration at the default rate of 1.0. 
			// If a seek was performed on the pipeline to play a different segment, 
			// this query will return the range specified in the last seek.
			start_pos = gst_segment_to_stream_time(&demux->segment, format, demux->segment.start);
			if ((stop_pos = demux->segment.stop) == -1)
				stop_pos = demux->segment.duration;
			else
				stop_pos = gst_segment_to_stream_time(&demux->segment, format, demux->segment.stop);


			// Set the segment query result
			gst_query_set_segment(query, playback_rate, format, start_pos, stop_pos);
			result = TRUE;
		}

		default:
			// just call the default handler
			result = gst_pad_query_default(pad, parent, query);
			break;
	}

	return result;
}

/*
 * Change the state in the element
 */
static GstStateChangeReturn 
gst_iestsdemux_change_state(GstElement * element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	Gstiestsdemux *demux = GST_IESTSDEMUX(element);
	g_assert_nonnull(demux);


	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		GST_DEBUG("State Change: NULL to READY.");

	case GST_STATE_CHANGE_READY_TO_PAUSED:
		GST_DEBUG("State Change: READY to PAUSED.");
		break;

	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		GST_DEBUG("State Change: PAUSED to PLAYING.");
		break;

	default:
		break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {

	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		GST_DEBUG("State Change: PLAYING to PAUSED.");
		break;

	case GST_STATE_CHANGE_PAUSED_TO_READY:
		GST_DEBUG("State Change: PAUSED to READY.");
		gst_adapter_clear(demux->sink_buffio_info->gst_adapter);
		av_streams_close(demux);

		// TODO: Revisit
		demux->have_group_id = FALSE;
		demux->group_id = G_MAXUINT;
		break;

	case GST_STATE_CHANGE_READY_TO_NULL:
		GST_DEBUG("State Change: READY to NULL.");
		break;

	default:
		break;
	}

	return ret;
}

/*
 * Send the event to the element
 */
static gboolean 
gst_iestsdemux_send_event(GstElement * element, GstEvent * event)
{
	gboolean result = FALSE;
	Gstiestsdemux *demux = GST_IESTSDEMUX(element);
	g_assert_nonnull(demux);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_SEEK:
		result = gst_iestsdemux_do_seek(demux, event);
		gst_event_unref(event);
		break;

	default:
		break;
	}

	return result;
}

/*
 * 
 */
static void 
gst_iestsdemux_type_find(GstTypeFind * find, gpointer user_data)
{
	GST_INFO("Checking the type...");

	GstStaticCaps sink_static_caps = TSDEMUX_SINK_STATIC_CAPS;

	GstCaps* sink_caps = gst_static_caps_get(&sink_static_caps);

	// Return the suggestion for the possible type
	gst_type_find_suggest(find, GST_TYPE_FIND_POSSIBLE, sink_caps);

	gst_caps_unref(sink_caps);
}

/* 
 * Receive the data buffer from the upstream and do the actual processing
 */
static GstFlowReturn
gst_iestsdemux_chain(GstPad * pad, GstObject * parent, GstBuffer * buf)
{
	Gstiestsdemux *demux = GST_IESTSDEMUX(parent);
	g_assert_nonnull(demux);

	GstBufferedIOInfo *buffio_info = demux->sink_buffio_info;
	g_assert_nonnull(buffio_info);

	g_mutex_lock(&buffio_info->io_sync_mutex);

	// Push the buffer to the adapter
	GST_DEBUG("Push the buffer to the adapter. Buff Size=%" G_GSIZE_FORMAT " bytes", gst_buffer_get_size(buf));
	gst_adapter_push(buffio_info->gst_adapter, buf);

	// Notify that the adapter has enough data to be processed
	buf = NULL;
	while (gst_adapter_available(buffio_info->gst_adapter) >= buffio_info->io_read_needed) {
		g_cond_signal(&buffio_info->io_sync_cond);
		g_cond_wait(&buffio_info->io_sync_cond, &buffio_info->io_sync_mutex);
	}

	g_mutex_unlock(&buffio_info->io_sync_mutex);

	return GST_FLOW_OK;
}

/*
 * Demux the streams and push the packet to the downstream
 * This function will be called periodically by using GstTask
 */
static void 
gst_iestsdemux_loop(Gstiestsdemux * demux)
{
	GstAVStream *gst_stream = NULL;
	GstBuffer *buff_push = NULL;

	gst_stream = av_streams_demux(demux, &buff_push);

	// Pushed the buffer to the downstream
	if (gst_stream != NULL && buff_push != NULL) {
		GstFlowReturn result;
		GST_DEBUG("Pushing the buffer");
		result = gst_pad_push(gst_stream->srcpad, buff_push);

		result = gst_flow_combiner_update_flow(demux->flow_combiner, result);
		if (result != GST_FLOW_OK) {
			GST_WARNING("Fail to update the flow combiner: %s", gst_flow_get_name(result));
		}
	}

	return;
}

/*
 * Activate the push mode in the sink pad
 */
static gboolean
gst_iestsdemux_sink_activate_pushmode(GstPad * sinkpad, GstObject * parent, gboolean active)
{
	gboolean result = FALSE;
	Gstiestsdemux *demux = (Gstiestsdemux *)parent;
	g_assert_nonnull(demux);

	GstBufferedIOInfo *buffio_info = demux->sink_buffio_info;
	g_assert_nonnull(buffio_info);
	
	if (active) {
		buffio_info->is_eos = FALSE;
		buffio_info->is_pullmode = demux->is_sink_pullmode;	// TODO: can i remove demux->is_sink_pullmode?
		result = gst_task_start(demux->push_task);
	}
	else {

		result = gst_task_stop(demux->push_task);

		result = gst_task_join(demux->push_task);
	}

	return result;
}

/*
 * Activate the pull mode in the sink pad
 */
static gboolean
gst_iestsdemux_sink_activate_pullmode(GstPad * sinkpad, GstObject * parent, gboolean active)
{
	gboolean result = FALSE;
	Gstiestsdemux *demux = (Gstiestsdemux *)parent;
	g_assert_nonnull(demux);

	GstBufferedIOInfo *buffio_info = demux->sink_buffio_info;
	g_assert_nonnull(buffio_info);

	if (active) {
		buffio_info->is_eos = FALSE;
		buffio_info->is_pullmode = demux->is_sink_pullmode;
		result = gst_pad_start_task(sinkpad, (GstTaskFunction)gst_iestsdemux_loop, demux, NULL);
	}
	else {
		// JK DEBUG
		result = gst_pad_stop_task(sinkpad);
	}

	return result;
}

/*
 * Activate the sink pad and decide the pull or push mode
 */
static gboolean
gst_iestsdemux_sink_activate(GstPad * sinkpad, GstObject * parent)
{
	GstQuery *query;
	gboolean result = FALSE;
	gboolean pull_mode = FALSE;
	GstSchedulingFlags flags;

	Gstiestsdemux *demux = (Gstiestsdemux *)parent;
	g_assert_nonnull(demux);

	query = gst_query_new_scheduling();
	g_assert_nonnull(query);

	result = gst_pad_peer_query(sinkpad, query);
	if (result) {
		pull_mode = gst_query_has_scheduling_mode_with_flags(query, GST_PAD_MODE_PULL, GST_SCHEDULING_FLAG_SEEKABLE);

		gst_query_parse_scheduling(query, &flags, NULL, NULL, NULL);
		if (flags & GST_SCHEDULING_FLAG_SEQUENTIAL)
			pull_mode = FALSE;
	}

	gst_query_unref(query);

	if (pull_mode) {
		GST_INFO("The pulling mode in the sink pad is activated.");
		demux->is_sink_pullmode = TRUE;
		result = gst_pad_activate_mode(sinkpad, GST_PAD_MODE_PULL, TRUE);
	}
	else {
		GST_INFO("Tthe pushing mode in the sink pad is activated.");
		demux->is_sink_pullmode = FALSE;
		result = gst_pad_activate_mode(sinkpad, GST_PAD_MODE_PUSH, TRUE);
	}

	return result;
}

/*
 * Activate the push/pull mode in the sink pad
 */
static gboolean
gst_iestsdemux_sink_activate_mode(GstPad * sinkpad, GstObject * parent, GstPadMode mode, gboolean active)
{
	gboolean ret = TRUE;

	switch (mode) {
	case GST_PAD_MODE_PUSH:
		ret = gst_iestsdemux_sink_activate_pushmode(sinkpad, parent, active);
		break;

	case GST_PAD_MODE_PULL:
		ret = gst_iestsdemux_sink_activate_pullmode(sinkpad, parent, active);
		break;

	default:
		ret = FALSE;
		break;
	}

	return ret;
}

/*
 * Perform the seek operation
 */
static gboolean
gst_iestsdemux_do_seek(Gstiestsdemux * demux, GstEvent * sk_event)
{
	gboolean result = FALSE;
	gdouble playback_rate;
	GstFormat stream_format;
	GstSeekFlags sk_flags;
	GstSeekType sk_start_type, sk_stop_type;
	gint64 sk_start_pos, sk_stop_pos;
	GstSegment sk_segment;

	if (!demux->is_sink_pullmode) {
		GST_DEBUG("The seeking is not managed by TS Demux since it is operated in the push mode.");
		return FALSE;
	}

	if (sk_event) {
		gst_event_parse_seek(sk_event, &playback_rate, &stream_format, &sk_flags, 
			&sk_start_type, &sk_start_pos, &sk_stop_type, &sk_stop_pos);

		// TODO: Need to convert the format?
		g_assert_true(demux->segment.format == stream_format);
	}
	else {
		sk_flags = 0;
	}

	gboolean flush = sk_flags & GST_SEEK_FLAG_FLUSH;
	if (flush) {
		gst_pad_push_event(demux->sinkpad, gst_event_new_flush_start());
		gst_iestsdemux_push_event_to_srcpads(demux, gst_event_new_flush_start());
	}
	else {
		gst_pad_pause_task(demux->sinkpad);
	}

	GST_PAD_STREAM_LOCK(demux->sinkpad);

	memcpy(&sk_segment, &demux->segment, sizeof(GstSegment));

	if (sk_event) {
		gboolean sk_update;
		gst_segment_do_seek(&sk_segment, playback_rate, stream_format, sk_flags, 
			sk_start_type, sk_start_pos, sk_stop_type, sk_stop_pos, &sk_update);
	}

	if (flush) {
		gst_pad_push_event(demux->sinkpad, gst_event_new_flush_stop(TRUE));
	}

	result = av_streams_seek(demux, &sk_segment);

	if (flush) {
		gst_iestsdemux_push_event_to_srcpads(demux, gst_event_new_flush_stop(TRUE));
	}

	if (result) {
		memcpy(&demux->segment, &sk_segment, sizeof(GstSegment));

		if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
			GstMessage * msg = gst_message_new_segment_start(GST_OBJECT(demux), demux->segment.format, demux->segment.position);
			gst_element_post_message(GST_ELEMENT(demux), msg);
		}

		gst_iestsdemux_push_event_to_srcpads(demux, gst_event_new_segment(&demux->segment));
	}

	gst_flow_combiner_reset(demux->flow_combiner);

	gst_pad_start_task(demux->sinkpad, (GstTaskFunction)gst_iestsdemux_loop, demux, NULL);

	GST_PAD_STREAM_UNLOCK(demux->sinkpad);
	
	return result;
}

/*
 * Push the event to all the source pads
 */
static gboolean
gst_iestsdemux_push_event_to_srcpads(Gstiestsdemux * demux, GstEvent * gst_event)
{
	gboolean result = FALSE;

	for (int i = 0; i < demux->num_of_all_streams; i++) {
		GstAVStream *stream = demux->av_streams[i];
		if (stream != NULL && stream->srcpad != NULL) {
			gst_event_ref(gst_event);
			result &= gst_pad_push_event(stream->srcpad, gst_event);
		}
	}

	gst_event_unref(gst_event);

	return result;
}

/*
* Push the tag events to all the source pads
*/
static void 
gst_iestsdemux_push_tags_to_srcpads(Gstiestsdemux * demux)
{
	GstTagList * tag_list = NULL;

	for (int i = 0; i < demux->num_of_all_streams; i++) {
		GstAVStream* gst_stream = demux->av_streams[i];
		if (gst_stream != NULL && gst_stream->srcpad) {
			// Handle the container tags
			tag_list = demux->tags;
			if (tag_list != NULL) {
				GstEvent *gst_event = gst_event_new_tag(gst_tag_list_ref(tag_list));
				gst_pad_push_event(gst_stream->srcpad, gst_event);
			}

			// Handle the stream tags
			tag_list = gst_stream->tags;
			if (tag_list != NULL) {
				GstEvent *gst_event = gst_event_new_tag(gst_tag_list_ref(tag_list));
				gst_pad_push_event(gst_stream->srcpad, gst_event);
			}
		}
	}
}

/* 
 * Entry point to initialize the plug-in.
 * initialize the plug-in itself and register the element factories and other features
 */
static gboolean
iestsdemux_init(GstPlugin * iestsdemux)
{
	// debug category for fltering log messages
	GST_DEBUG_CATEGORY_INIT(gst_iestsdemux_debug, "IESTsDemux", 1, "IES MPEG TS Demuxer");
	
	init_avdemux();

	GstStaticCaps sink_static_caps = TSDEMUX_SINK_STATIC_CAPS;
	GstCaps * possible_caps = gst_static_caps_get(&sink_static_caps);

	if (!gst_element_register(iestsdemux, "iestsdemux", GST_RANK_NONE, GST_TYPE_IESTSDEMUX) ||
		!gst_type_find_register(iestsdemux, TSDEMUX_TYPEFIND_NAME, GST_RANK_NONE, 
			gst_iestsdemux_type_find, NULL, possible_caps, NULL, NULL)) {
		gst_caps_unref(possible_caps);
		return FALSE;
	}

	gst_caps_unref(possible_caps);
	return TRUE;
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "IES PlugIns"
#endif

 /* gstreamer looks for this structure to register iestsdemuxs */
GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	iestsdemux,
	"Plugin is used to demux the MPEG TS",
	iestsdemux_init,
	"V0.0.1",
	"LGPL",
	"Immersive Experiences SDK",
	"Unknown - TODO"
)

/*
 * Open the stream through libav
 */
static gboolean 
av_streams_open(Gstiestsdemux * demux)
{
	gboolean success = TRUE;
	gint av_error = 0;

	GstiestsdemuxClass *klass = (GstiestsdemuxClass *)G_OBJECT_GET_CLASS(demux);
	GstBufferedIOInfo * buffio_info = demux->sink_buffio_info;
	AVFormatContext * fmt_ctx = NULL;

	g_assert_nonnull(demux);
	g_assert_nonnull(klass);
	g_assert_nonnull(buffio_info);

	if (demux->is_opened)
		av_streams_close(demux);

	// Open the IO context
	av_error = av_bufferedio_open(buffio_info);
	if (av_error < 0) {
		goto ex_averror;
	}

	// Allocate the memory for the video format
	demux->av_format_context = avformat_alloc_context();
	fmt_ctx = demux->av_format_context;
	fmt_ctx->pb = buffio_info->io_context;
	fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

	// Open the video stream
	av_error = avformat_open_input(&fmt_ctx, NULL, klass->av_in_format, NULL);
	if (av_error < 0) goto ex_averror;

	// Retrieve stream information
	av_error = avformat_find_stream_info(fmt_ctx, NULL);
	if (av_error < 0) goto ex_averror;

	demux->num_of_all_streams = fmt_ctx->nb_streams;
	for (int i = 0; i < demux->num_of_all_streams; i++) {
		av_streams_parse_stream(demux, fmt_ctx->streams[i], i);
	}

	gst_element_no_more_pads(GST_ELEMENT(demux));

	// TODO: Revist transform some useful info to GstClockTime and remember
	demux->start_time = gst_util_uint64_scale_int(fmt_ctx->start_time, GST_SECOND, AV_TIME_BASE);
	GST_DEBUG("start time: %" GST_TIME_FORMAT, GST_TIME_ARGS(demux->start_time));
	if (fmt_ctx->duration > 0)
		demux->duration = gst_util_uint64_scale_int(fmt_ctx->duration, GST_SECOND, AV_TIME_BASE);
	else
		demux->duration = GST_CLOCK_TIME_NONE;

	GST_DEBUG("duration: %" GST_TIME_FORMAT, GST_TIME_ARGS(demux->duration));

	/* store duration in the segment as well */
	demux->segment.duration = demux->duration;

	// TODO: what are the seek_event and cached event?

	// Send the segment
	GST_DEBUG("Sending segment %" GST_SEGMENT_FORMAT, &demux->segment);
	gst_iestsdemux_push_event_to_srcpads(demux, gst_event_new_segment(&demux->segment));

	// Parse metadata to GST Tags and send tag events
	av_streams_parse_metadata_to_taglists(demux);
	gst_iestsdemux_push_tags_to_srcpads(demux);

	demux->is_opened = TRUE;

	goto fn_done;

ex_averror:
	GST_PRINT_AVERROR(av_error);
	success = FALSE;

fn_done:
	return success;
}

/*
 * Close the stream
 */
static void 
av_streams_close(Gstiestsdemux * demux)
{
	g_assert_nonnull(demux);

	if (!demux->is_opened)
		return;
	
	// Remove pads
	for (int i = 0; i < demux->num_of_all_streams; i++) {
		GstAVStream* stream = demux->av_streams[i];
		if (stream != NULL) {
			if (stream->srcpad != NULL) {
				// Remove the src pad from the flow combiner
				gst_flow_combiner_remove_pad(demux->flow_combiner, stream->srcpad);

				// Remove the src pad from the element
				gst_element_remove_pad(GST_ELEMENT(demux), stream->srcpad);
			}

			if (stream->tags)
				gst_tag_list_unref(stream->tags);

			g_free(stream);
		}
		demux->av_streams[i] = NULL;
	}

	av_bufferedio_close(demux->av_format_context->pb);
	demux->av_format_context->pb = NULL;

	if (demux->av_format_context != NULL) {
		avformat_close_input(&demux->av_format_context);
		avformat_free_context(demux->av_format_context);
		demux->av_format_context = NULL;
	}

	if (demux->tags)
		gst_tag_list_unref(demux->tags);

	demux->is_opened = FALSE;

	gst_segment_init(&demux->segment, GST_FORMAT_TIME);
}

/*
 * Parse each stream and collect its infos
 */
static gboolean
av_streams_parse_stream(Gstiestsdemux * demux, AVStream * av_stream, int index)
{
	gboolean result = FALSE;
	AVCodecContext *codec_context = NULL;
	GstAVStream *gst_stream = NULL;

	GstPadTemplate *templ = NULL;	// JK DEBUG
	GstPad *pad = NULL;
	GstCaps *caps = NULL;
	int pad_index = -1;
	int av_error = 0;

	GstiestsdemuxClass *klass = (GstiestsdemuxClass *)G_OBJECT_GET_CLASS(demux);

	g_assert_nonnull(demux);
	g_assert_nonnull(klass);

	codec_context = avcodec_alloc_context3(NULL);
	if (codec_context == NULL) {
		GST_DEBUG("Failed to allocate the codec context!");
		av_error = AVERROR(EINVAL);
		goto ex_averror;
	}
	avcodec_parameters_to_context(codec_context, av_stream->codecpar);

	gst_stream = g_new0(GstAVStream, 1);
	demux->av_streams[av_stream->index] = gst_stream;

	gst_stream->srcpad = NULL;
	gst_stream->avstream = av_stream;
	gst_stream->av_media_type = codec_context->codec_type;
	gst_stream->has_discontinuity = TRUE;
	gst_stream->ts_last_pos = GST_CLOCK_TIME_NONE;
	gst_stream->tags = NULL;

	// TODO: Currently we are getting the first stream of the each media type
	switch (codec_context->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
		{
			if (codec_context->codec_id != AV_CODEC_ID_H264)
				break;
			if (demux->active_video_stream_index == -1)
				demux->active_video_stream_index = index;
			pad_index = demux->num_of_video_streams++;
			templ = klass->video_src_template;

			// Create a caps
			if (codec_context->width != -1 && codec_context->height != -1) {
				caps = gst_caps_new_simple(TSDEMUX_SRC_VIDEO_MIME_TYPE,
					"width", G_TYPE_INT, codec_context->width,
					"height", G_TYPE_INT, codec_context->height,
					//"framerate", G_TYPE_DOUBLE, av_q2d(av_stream->r_frame_rate), NULL);
					"framerate", GST_TYPE_FRACTION, 1, 1, NULL);	// TODO
			}
			else {
				caps = gst_caps_new_empty_simple(TSDEMUX_SRC_VIDEO_MIME_TYPE);
			}

			gst_caps_set_simple(caps, "alignment", G_TYPE_STRING, "au",
				"stream-format", G_TYPE_STRING, "byte-stream", NULL);
			break;
		}

		case AVMEDIA_TYPE_AUDIO:
		{
			if (codec_context->codec_id != AV_CODEC_ID_AAC)
				break;
			if (demux->active_audio_stream_index == -1)
				demux->active_audio_stream_index = index;
			pad_index = demux->num_of_audio_streams++;
			templ = klass->audio_src_template;

			if (codec_context->channels != -1) {
				caps = gst_caps_new_simple(TSDEMUX_SRC_AUDIO_MIME_TYPE,
					"rate", G_TYPE_INT, codec_context->sample_rate,
					"channels", G_TYPE_INT, codec_context->channels, NULL);

				// TODO: is the "channel-mask" is required?
			}
			else {
				caps = gst_caps_new_empty_simple(TSDEMUX_SRC_AUDIO_MIME_TYPE);
			}

			gst_caps_set_simple(caps, "mpegversion", G_TYPE_INT, 4,
				"base-profile", G_TYPE_STRING, "lc", NULL);
			break;
		}

		case AVMEDIA_TYPE_DATA:
		{
			if (demux->active_metadata_stream_index == -1)
				demux->active_metadata_stream_index = index;
			pad_index = demux->num_of_metadata_streams++;
			templ = klass->metadata_src_template;

			caps = gst_caps_new_empty_simple(TSDEMUX_SRC_METADATA_MIME_TYPE);
			break;
		}

		default:
			goto ex_stream_ignored;
	}

	if (caps == NULL)
		goto ex_stream_ignored;

	// Create new pad
	gchar * padname = g_strdup_printf(GST_PAD_TEMPLATE_NAME_TEMPLATE(templ), pad_index);
	GST_DEBUG("Creating a pad (%s)", padname);

	pad = gst_pad_new_from_template(templ, padname);
	g_free(padname);

	gst_pad_use_fixed_caps(pad);
	gst_pad_set_active(pad, TRUE);

	gst_pad_set_query_function(pad, gst_iestsdemux_src_query);
	gst_pad_set_event_function(pad, gst_iestsdemux_src_event);

	gst_stream->srcpad = pad;
	gst_pad_set_element_private(pad, gst_stream);

	// TODO: Rewrite
	gchar *stream_id = gst_pad_create_stream_id_printf(pad, GST_ELEMENT_CAST(demux), "%03u",
			av_stream->index);

	GstEvent *gst_event = gst_pad_get_sticky_event(demux->sinkpad, GST_EVENT_STREAM_START, 0);
	if (gst_event) {
		if (gst_event_parse_group_id(gst_event, &demux->group_id))
			demux->have_group_id = TRUE;
		else
			demux->have_group_id = FALSE;
		gst_event_unref(gst_event);
	}
	else if (!demux->have_group_id) {
		demux->have_group_id = TRUE;
		demux->group_id = gst_util_group_id_next();
	}
	gst_event = gst_event_new_stream_start(stream_id);
	if (demux->have_group_id)
		gst_event_set_group_id(gst_event, demux->group_id);

	gst_pad_push_event(pad, gst_event);
	g_free(stream_id);

	GST_INFO_OBJECT(pad, "adding pad with caps %" GST_PTR_FORMAT, caps);
	gst_pad_set_caps(pad, caps);
	gst_caps_unref(caps);

	// Add and activate the pad
	gst_element_add_pad(GST_ELEMENT(demux), pad);

	// Add the pad to the flow combiner
	gst_flow_combiner_add_pad(demux->flow_combiner, pad);

	result = TRUE;
	goto done;

ex_averror:
	GST_PRINT_AVERROR(av_error);
	if (gst_stream)
		g_free(gst_stream);
	result = FALSE;
	goto done;

ex_stream_ignored:
	GST_INFO("The media type (%d) will be ignored.", codec_context->codec_type);
	result = FALSE;
	goto done;

done:
	if (codec_context)
		avcodec_free_context(&codec_context);

	return result;
}

/*
 * Seek the desired position
 */
static gboolean
av_streams_seek(Gstiestsdemux * demux, GstSegment * segment)
{
	GstClockTime gst_target_ts = 0, av_target_ts = 0;
	gint index;
	AVStream * av_stream;
	int av_error = 0;
	gboolean result = FALSE;

	// Find the default stream used for the seeking
	index = av_find_default_stream_index(demux->av_format_context);
	g_return_val_if_fail(index >= 0, FALSE);
	av_stream = demux->av_format_context->streams[index];

	// Compute the position within 0 and the duration
	gst_target_ts = segment->position + demux->start_time;
	av_target_ts = convert_timestamp_from_gst_to_av(gst_target_ts, av_stream->time_base);

	GST_DEBUG("Seek to %" GST_TIME_FORMAT, GST_TIME_ARGS(gst_target_ts));

	// TODO: GstSegmentFlag does not contain GST_SEEK_FLAG_KEY_UNIT
	if (segment->flags & GST_SEEK_FLAG_KEY_UNIT) {
		gint keyframe_index;

		keyframe_index = av_index_search_timestamp(av_stream, av_target_ts, AVSEEK_FLAG_BACKWARD);

		GST_DEBUG("Keyframe index = %d", keyframe_index);

		if (keyframe_index > 0) {
			av_target_ts = av_stream->index_entries[keyframe_index].timestamp;
			gst_target_ts = convert_timestamp_from_av_to_gst(av_target_ts, av_stream->time_base);

			// LOG
		}
	}

	// Seek to the frame
	av_error = av_seek_frame(demux->av_format_context, index, av_target_ts, AVSEEK_FLAG_BACKWARD);
	if (av_error < 0) {
		GST_DEBUG("Fail to seek!!!");
		goto ex_averror;
	}
	
	// Adjust the ttime
	if (gst_target_ts > demux->start_time)
		gst_target_ts -= demux->start_time;
	else
		gst_target_ts = 0;

	// Set the time&position with a new value
	segment->position = gst_target_ts;
	segment->time = gst_target_ts;
	segment->start = gst_target_ts;

	result = TRUE;
	goto fn_done;

ex_averror:
	GST_PRINT_AVERROR(av_error);

fn_done:
	return result;
}

static GstAVStream *
av_streams_demux(Gstiestsdemux * demux, GstBuffer ** gst_buff)
{
	AVPacket *packet = NULL;
	GstAVStream *gst_stream = NULL;
	GstClockTime position, duration;
	GstBuffer *buff_push = NULL;
	gint buff_size;
	gint64 packet_pts = 0;
	gint av_error = 0;

	g_assert_nonnull(demux);

	// Open the streams unless it has already opened
	if (!demux->is_opened)
	{
		if (!av_streams_open(demux)) {
			GST_ERROR("Fail to open the stream!!!");
			goto fn_done;
		}
	}

	// Allocate a packet
	packet = av_packet_alloc();
	if (packet == NULL) {
		av_error = AVERROR(ENOMEM);
		goto ex_averror;
	}

	// Read the frame
	av_error = av_read_frame(demux->av_format_context, packet);
	if (av_error < 0) {
		if (av_error == (int)AVERROR_EOF)
			goto ex_eos;

		GST_ERROR("Fail to Read the frame!!!");
		goto ex_averror;
	}

	gst_stream = demux->av_streams[packet->stream_index];
	if (gst_stream == NULL) {
		GST_WARNING("Could not find the stream with the specified index:%d", packet->stream_index);
		goto fn_done;
	}

	packet_pts = packet->pts;
	if (packet_pts < 0) packet_pts = 0;

	// Get the postion and duration
	// TODO: Rewrite
	position = convert_timestamp_from_av_to_gst(packet_pts, gst_stream->avstream->time_base);
	if (GST_CLOCK_TIME_IS_VALID(position)) {
		gst_stream->ts_last_pos = position;
	}

	duration = convert_timestamp_from_av_to_gst(packet->duration, gst_stream->avstream->time_base);
	if (duration <= 0) {
		GST_DEBUG("invalid buffer duration, setting to NONE");	// TODO: Is it a warning or error?
		duration = GST_CLOCK_TIME_NONE;
	}

	GST_DEBUG("Packet Info: pts=%" GST_TIME_FORMAT " / size=%d / stream_index=%d / flags=%d / duration=%" GST_TIME_FORMAT " / pos=%" G_GINT64_FORMAT,
		GST_TIME_ARGS(position), packet->size, packet->stream_index, packet->flags, GST_TIME_ARGS(duration), (gint64)packet->pos);

	// Adjust the timestamp
	if (GST_CLOCK_TIME_IS_VALID(position)) {
		if (demux->start_time >= position)
			position = 0;
		else
			position -= demux->start_time;
	}

	// Check if the stream is out of range.
	if (demux->segment.stop != -1 && position > demux->segment.stop) {
		//TODO: Assume EOS
		goto ex_eos;
	}

	// Gather data/information about the buffer to be pushed
	if (packet->stream_index == demux->active_metadata_stream_index) {
		GST_DEBUG("Manipulate the id3 metadata");

		gint offset = demux->metadata_id3_prefix_size;
		buff_size = packet->size + offset;
		buff_push = gst_buffer_new_and_alloc(buff_size);

		gst_buffer_fill(buff_push, 0, demux->metadata_id3_prefix_buff, offset);
		gst_buffer_fill(buff_push, offset, packet->data, packet->size);
	}
	else {
		GST_DEBUG("Handle the video/audio data");

		buff_size = packet->size;
		buff_push = gst_buffer_new_and_alloc(buff_size);

		gst_buffer_fill(buff_push, 0, packet->data, buff_size);
	}

	GST_BUFFER_TIMESTAMP(buff_push) = position;
	GST_BUFFER_DURATION(buff_push) = duration;

	if (!(packet->flags & AV_PKT_FLAG_KEY)) {
		GST_BUFFER_FLAG_SET(buff_push, GST_BUFFER_FLAG_DELTA_UNIT);
	}

	// The first segment should turn on the discontinuity flag
	// TODO: How can we recognize the discontinuous buffer???
	if (gst_stream->has_discontinuity) {
		GST_BUFFER_FLAG_SET(buff_push, GST_BUFFER_FLAG_DISCONT);
		gst_stream->has_discontinuity = FALSE;
	}

	*gst_buff = buff_push;
	goto fn_done;

ex_eos:
	GST_DEBUG("The stream reaches the end.");

	gst_pad_pause_task(demux->sinkpad);

	if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
		gint64 stop;

		if ((stop = demux->segment.stop) == -1)
			stop = demux->segment.duration;

		GST_LOG("Post a message to notify the end segment.");
		GstMessage *gst_msg = gst_message_new_segment_done(GST_OBJECT(demux), demux->segment.format, stop);
		gst_element_post_message(GST_ELEMENT(demux), gst_msg);

		GST_LOG("Send an event to notify the end segment.");
		GstEvent *gst_event = gst_event_new_segment_done(demux->segment.format, stop);
		gst_iestsdemux_push_event_to_srcpads(demux, gst_event);
	}
	else {
		GST_LOG("pushing eos");
		gst_iestsdemux_push_event_to_srcpads(demux, gst_event_new_eos());
	}

	goto fn_done;

ex_averror:
	gst_pad_pause_task(demux->sinkpad);
	GST_PRINT_AVERROR(av_error);

fn_done:
	if (packet != NULL)
		av_packet_unref(packet);

	return gst_stream;
}

static void
av_streams_parse_metadata_to_taglists(Gstiestsdemux * demux)
{
	AVDictionaryEntry * tag = NULL;
	AVDictionary *av_metadata_dict = NULL;

	// Poplulate tags from the container
	if (demux->tags) gst_tag_list_unref(demux->tags);
	demux->tags = gst_tag_list_new_empty();
	av_metadata_dict = demux->av_format_context->metadata;
	while ((tag = av_dict_get(av_metadata_dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
		GST_INFO("Reading a tag: %s = %s", tag->key, tag->value);	// TODO: Convert to GST_LOG
		// TODO: Selectively populate the tags
		//if (g_str_equal(tag->key, GST_TAG_TITLE)) {

		//}
		gst_tag_list_add(demux->tags, GST_TAG_MERGE_REPLACE, tag->key, tag->value, NULL);
	}

	// Populate tags from each stream
	for (int i = 0; i < demux->num_of_all_streams; i++) {
		GstAVStream* gst_stream = demux->av_streams[i];
		if (gst_stream != NULL) {
			if (gst_stream->tags) gst_tag_list_unref(gst_stream->tags);
			gst_stream->tags = gst_tag_list_new_empty();
			av_metadata_dict = gst_stream->avstream->metadata;
			while ((tag = av_dict_get(av_metadata_dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
				GST_INFO("Reading a tag: %s = %s", tag->key, tag->value);	// TODO: Convert to GST_LOG
				// TODO: Selectively populate the tags
				gst_tag_list_add(gst_stream->tags, GST_TAG_MERGE_REPLACE, tag->key, tag->value, NULL);
			}
		}
	}
}

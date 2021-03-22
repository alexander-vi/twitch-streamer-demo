// (c) Alexander Voitenko 2021 - present

#include <gst/gst.h>
#include <linux/limits.h>

#include <stdio.h>
#include <unistd.h>

// Max number of sources (video files) used for processing
// Note: currently this number can not be greater than 3 because videomixer's layout is hardcoded, so to make this
//       number dynamic, layout creation should be refined. See setup_videomixer_layout function for details
#define MAX_SOURCES 3

// Index of source file where extract audio track
#define AUDIO_FROM_SOURCE_INDEX 0

// Output video parameters
#define OUTPUT_VIDEO_WIDTH 1280
#define OUTPUT_VIDEO_HEIGHT 720

#define TWITCH_URL_PREFIX "rtmp://live.justin.tv/app"

// Structure to contain all our information, so we can pass it everywhere
typedef struct _ApplicationContext {
    GstElement* pipeline;

    gboolean streaming_enabled;
    const char* source_paths[MAX_SOURCES];
    const char* twitch_api_key;

    GstElement* source[MAX_SOURCES];

    GstElement* audio_convert;
    GstElement* audio_resample;
    GstElement* audio_sink[MAX_SOURCES];
    GstElement* audio_tee;
    GstElement* stream_audio_queue;
    GstElement* device_audio_queue;
    GstElement* audio_device_sink;
    GstElement* voaacenc;

    GstElement* video_scale[MAX_SOURCES];
    GstElement* videomixer;
    GstElement* video_tee;
    GstElement* stream_video_queue;
    GstElement* device_video_queue;
    GstElement* video_device_sink;
    GstElement* x264enc;
    GstElement* flv_mux;
    GstElement* rtmp_sink;
} ApplicationContext;

static int parse_command_line(int argc, char* argv[], ApplicationContext* data);
static void print_usage();
static int create_pipeline(ApplicationContext* data);
static int run_pipeline(ApplicationContext* data);
static void free_resources(ApplicationContext* data);

// Handler for the pad-added signal
static void pad_added_handler(GstElement* src, GstPad* pad, ApplicationContext* data);

int main(int argc, char* argv[]) {
    ApplicationContext data;
    int return_code = 0;

    g_print("Parsing command line...\n");
    if (parse_command_line(argc, argv, &data) != 0) {
        print_usage();
        return_code = 1;
        goto exit;
    }
    g_print("Done.\n");

    g_print("Initializing GStreamer...\n");
    gst_init(NULL, NULL);
    g_print("Done.\n");

    g_print("Creating pipeline...\n");
    if (create_pipeline(&data) != 0) {
        g_printerr("Error: unable to setup pipeline\n");
        return_code = 1;
        goto exit;
    }
    g_print("Done.\n");

    g_print("Running pipeline...\n");
    if (run_pipeline(&data) != 0) {
        g_printerr("Error: unable to run pipeline\n");
        return_code = 1;
        goto exit;
    }
    g_print("Done.\n");

exit:
    free_resources(&data);

    return return_code;
}

static int parse_command_line(int argc, char* argv[], ApplicationContext* data) {
    int i;

    if (argc != MAX_SOURCES + 1 && argc != MAX_SOURCES + 2) {
        return 1;
    }

    data->streaming_enabled = argc == MAX_SOURCES + 2;

    if (data->streaming_enabled) {
        g_print("Twitch streaming is enabled!\n");
        data->twitch_api_key = argv[1];
        for (i = 0; i < MAX_SOURCES; ++i) {
            data->source_paths[i] = argv[i + 2];
        }
    } else {
        g_print("Twitch streaming is NOT enabled, because twitch API key was not specified!\n");
        data->twitch_api_key = "";
        for (i = 0; i < MAX_SOURCES; ++i) {
            data->source_paths[i] = argv[i + 1];
        }
    }

    return 0;
}

static void print_usage() {
    g_print(
        "Application to stream mixed video data to twitch.\n"
        "Usage:\n  ./twitch-streamer [twitch_api_key] video_path_1 video_path_2 video_path_3\n"
        "Examples:\n  ./twitch-streamer live_111111111_aaaabbbcccddddeeeeffffggghhhhh ../data/sintel_trailer-480p.webm "
        "../data/big_buck_bunny_trailer-360p.mp4 ../data/the_daily_dweebs-720p.mp4\n"
        "  ./twitch-streamer ../data/sintel_trailer-480p.webm ../data/big_buck_bunny_trailer-360p.mp4 "
        "../data/the_daily_dweebs-720p.mp4\n");
}

#define ENSURE_INITED(X, Y)                                                                  \
    if (!X->Y) {                                                                             \
        char buf[255];                                                                       \
        snprintf(buf, sizeof(buf), "Error: pipeline element '%s', was not created\n", (#Y)); \
        g_printerr("%s", buf);                                                               \
        return 1;                                                                            \
    }

static int create_pipeline_elements(ApplicationContext* data) {
    int i;
    char string_buf[PATH_MAX + 1024];

    for (i = 0; i < MAX_SOURCES; ++i) {
        snprintf(string_buf, sizeof(string_buf), "source_%i", i);
        data->source[i] = gst_element_factory_make("uridecodebin", string_buf);
    }

    // Audio
    data->audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    data->audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    data->audio_tee = gst_element_factory_make("tee", "audio_tee");
    if (data->streaming_enabled) {
        data->voaacenc = gst_element_factory_make("voaacenc", "aac_encoder");
        data->stream_audio_queue = gst_element_factory_make("queue", "stream_audio_queue");
    } else {
        data->stream_audio_queue = NULL;
        data->voaacenc = NULL;
    }

    for (i = 0; i < MAX_SOURCES; ++i) {
        if (i == AUDIO_FROM_SOURCE_INDEX) {
            data->audio_sink[i] = data->audio_tee;
        } else {
            snprintf(string_buf, sizeof(string_buf), "fake_audio_sink_%i", i);
            data->audio_sink[i] = gst_element_factory_make("fakesink", string_buf);
        }
    }
    data->audio_device_sink = gst_element_factory_make("autoaudiosink", "audio_device_sink");
    data->device_audio_queue = gst_element_factory_make("queue", "device_audio_queue");

    // Video
    for (i = 0; i < MAX_SOURCES; ++i) {
        snprintf(string_buf, sizeof(string_buf), "video_scale_%i", i);
        data->video_scale[i] = gst_element_factory_make("videoscale", string_buf);
    }
    data->videomixer = gst_element_factory_make("videomixer", "video_mixer");
    data->video_tee = gst_element_factory_make("tee", "video_tee");
    if (data->streaming_enabled) {
        data->stream_video_queue = gst_element_factory_make("queue", "stream_video_queue");
        data->x264enc = gst_element_factory_make("x264enc", "x264_enc");
        data->flv_mux = gst_element_factory_make("flvmux", "flv_mux");
        data->rtmp_sink = gst_element_factory_make("rtmpsink", "rtmp_sink");
    } else {
        data->stream_video_queue = NULL;
        data->x264enc = NULL;
        data->flv_mux = NULL;
        data->rtmp_sink = NULL;
    }
    data->device_video_queue = gst_element_factory_make("queue", "device_video_queue");
    data->video_device_sink = gst_element_factory_make("autovideosink", "video_device_sink");

    data->pipeline = gst_pipeline_new("twitch-pipeline");
    if (!data->pipeline) {
        g_printerr("Error: failed to create pipeline.\n");
        return 1;
    }

    // Ensure that everything was created properly
    // Audio
    for (i = 0; i < MAX_SOURCES; ++i) {
        ENSURE_INITED(data, source[i]);
    }
    ENSURE_INITED(data, audio_convert);
    ENSURE_INITED(data, audio_resample);
    ENSURE_INITED(data, audio_tee);
    if (data->streaming_enabled) {
        ENSURE_INITED(data, stream_audio_queue);
        ENSURE_INITED(data, voaacenc);
    }
    for (i = 0; i < MAX_SOURCES; ++i) {
        ENSURE_INITED(data, audio_sink[i]);
    }
    ENSURE_INITED(data, audio_device_sink);
    ENSURE_INITED(data, device_audio_queue);

    // Video
    for (i = 0; i < MAX_SOURCES; ++i) {
        ENSURE_INITED(data, video_scale[i]);
    }
    ENSURE_INITED(data, videomixer);
    ENSURE_INITED(data, video_tee);
    if (data->streaming_enabled) {
        ENSURE_INITED(data, stream_video_queue);
        ENSURE_INITED(data, x264enc);
        ENSURE_INITED(data, flv_mux);
        ENSURE_INITED(data, rtmp_sink);
    }
    ENSURE_INITED(data, device_video_queue);
    ENSURE_INITED(data, video_device_sink);

    // Setting elements properties
    char current_work_dir[PATH_MAX];
    if (getcwd(current_work_dir, sizeof(current_work_dir)) != NULL) {
        g_print("Current working dir: %s\n", current_work_dir);
    } else {
        g_printerr("Error: getcwd() error");
        return 1;
    }

    for (i = 0; i < MAX_SOURCES; ++i) {
        char tmp_buf[PATH_MAX];
        if (data->source_paths[i] && data->source_paths[i][0] == '/') { // absolute path
            snprintf(tmp_buf, sizeof(tmp_buf), "%s", data->source_paths[i]);
        } else {
            snprintf(tmp_buf, sizeof(tmp_buf), "%s/%s", current_work_dir, data->source_paths[i]);
        }

        if (access(tmp_buf, F_OK) != -1) {
            g_print("Loading file: '%s'\n", tmp_buf);
        } else {
            g_printerr("Error: file '%s' does not exist!\n", tmp_buf);
            return 1;
        }

        snprintf(string_buf, sizeof(tmp_buf), "file://%s", tmp_buf);
        g_object_set(data->source[i], "uri", string_buf, NULL);
    }

    if (data->streaming_enabled) {
        g_object_set(data->stream_audio_queue, "leaky", 2 /*downstream*/, NULL);
        g_object_set(data->stream_audio_queue, "max-size-time", 5 * GST_SECOND, NULL);
        g_object_set(data->stream_video_queue, "leaky", 2 /*downstream*/, NULL);
        g_object_set(data->stream_video_queue, "max-size-time", 5 * GST_SECOND, NULL);

        g_object_set(data->flv_mux, "streamable", TRUE, NULL);

        g_object_set(data->x264enc, "bitrate", 768, NULL);
        g_object_set(data->x264enc, "speed-preset", 4 /*Faster*/, NULL);
        g_object_set(data->x264enc, "qp-min", 30, NULL);
        g_object_set(data->x264enc, "tune", 4 /*Zero latency*/, NULL);

        snprintf(string_buf, sizeof(string_buf), "%s/%s", TWITCH_URL_PREFIX, data->twitch_api_key);
        g_object_set(data->rtmp_sink, "location", string_buf, NULL);
    }

    return 0;
}

static int add_elements_to_pipeline(ApplicationContext* data) {
    int i;

    for (i = 0; i < MAX_SOURCES; ++i) {
        if (!gst_bin_add(GST_BIN(data->pipeline), data->source[i])) {
            g_printerr("Error: failed to add data source %i.\n", i);
            return 1;
        }
    }

    for (i = 0; i < MAX_SOURCES; ++i) {
        // Element for real audio sink is added below explicitly
        if (i != AUDIO_FROM_SOURCE_INDEX) {
            if (!gst_bin_add(GST_BIN(data->pipeline), data->audio_sink[i])) {
                g_printerr("Error: failed to add audio sink %i.\n", i);
                return 1;
            }
        }
    }

    for (i = 0; i < MAX_SOURCES; ++i) {
        if (!gst_bin_add(GST_BIN(data->pipeline), data->video_scale[i])) {
            g_printerr("Error: failed to add video scale %i.\n", i);
            return 1;
        }
    }

    // All other elements
    gst_bin_add_many(GST_BIN(data->pipeline),
                     data->audio_convert,
                     data->audio_resample,
                     data->audio_device_sink,
                     data->audio_tee,
                     data->device_audio_queue,
                     data->videomixer,
                     data->video_tee,
                     data->device_video_queue,
                     data->video_device_sink,
                     NULL);

    if (data->streaming_enabled) {
        gst_bin_add_many(GST_BIN(data->pipeline),
                         data->stream_audio_queue,
                         data->voaacenc,
                         data->stream_video_queue,
                         data->flv_mux,
                         data->x264enc,
                         data->rtmp_sink,
                         NULL);
    }

    return 0;
}

static int setup_videomixer_layout(ApplicationContext* data) {
    GstPad* video_mixer_sink_pad[MAX_SOURCES];
    int result = 0;
    int i;
    char string_buf[255];

    for (i = 0; i < MAX_SOURCES; ++i) {
        snprintf(string_buf, sizeof(string_buf), "video_scale_%i", i);
        video_mixer_sink_pad[i] = gst_element_get_request_pad(data->videomixer, "sink_%u");
        if (!video_mixer_sink_pad[i]) {
            g_printerr("Error: failed to get pad %i from video mixer.\n", i);
            result = 1;
            goto exit;
        }
        g_print("Requested pad from videomixer: %s\n", GST_PAD_NAME(video_mixer_sink_pad[i]));
    }

    g_object_set(data->videomixer, "name", "mix", NULL);
    g_object_set(data->videomixer, "background", 1, NULL); // black
    g_object_set(video_mixer_sink_pad[0], "xpos", 0, NULL);
    g_object_set(video_mixer_sink_pad[0], "ypos", 0, NULL);
    g_object_set(video_mixer_sink_pad[1], "xpos", OUTPUT_VIDEO_WIDTH / 2, NULL);
    g_object_set(video_mixer_sink_pad[1], "ypos", 0, NULL);
    g_object_set(video_mixer_sink_pad[2], "xpos", OUTPUT_VIDEO_WIDTH / 4, NULL);
    g_object_set(video_mixer_sink_pad[2], "ypos", OUTPUT_VIDEO_HEIGHT / 2, NULL);

exit:
    for (i = 0; i < MAX_SOURCES; ++i) {
        if (video_mixer_sink_pad[i]) {
            gst_object_unref(video_mixer_sink_pad[i]);
        }
    }

    return result;
}

static int link_pipeline_elements(ApplicationContext* data) {
    int result = 0;
    int i;

    GstPad* audio_tee_src_pad_1 = NULL;
    GstPad* audio_tee_src_pad_2 = NULL;
    GstPad* video_tee_src_pad_1 = NULL;
    GstPad* video_tee_src_pad_2 = NULL;

    GstPad* stream_audio_queue_snk_pad = NULL;
    GstPad* device_audio_queue_snk_pad = NULL;
    GstPad* stream_video_queue_snk_pad = NULL;
    GstPad* device_video_queue_snk_pad = NULL;

    GstCaps* video_scale_caps;

    char string_buf[255];

    if (!gst_element_link_many(data->audio_convert, data->audio_resample, data->audio_tee, NULL)) {
        g_printerr("Error: audio elements could not be linked.\n");
        result = 1;
        goto exit;
    }

    snprintf(string_buf,
             sizeof(string_buf),
             "video/x-raw,width=%i,height=%i",
             OUTPUT_VIDEO_WIDTH / 2,
             OUTPUT_VIDEO_HEIGHT / 2);
    video_scale_caps = gst_caps_from_string(string_buf);
    if (!video_scale_caps) {
        g_printerr("Error: failed to create scale filter caps.\n");
        result = 1;
        goto exit;
    }

    for (i = 0; i < MAX_SOURCES; ++i) {
        snprintf(string_buf, sizeof(string_buf), "sink_%i", i);
        if (!gst_element_link_pads_filtered(
                data->video_scale[i], "src", data->videomixer, string_buf, video_scale_caps)) {
            g_printerr("Error: failed to link video %i with rescale filter.\n", i);
            result = 1;
            goto exit;
        }
    }

    gst_caps_unref(video_scale_caps);

    if (!gst_element_link_many(data->videomixer, data->video_tee, NULL)) {
        g_printerr("Error: mixer output elements could not be linked.\n");
        result = 1;
        goto exit;
    }

    audio_tee_src_pad_1 = gst_element_get_request_pad(data->audio_tee, "src_%u");
    video_tee_src_pad_1 = gst_element_get_request_pad(data->video_tee, "src_%u");
    if (data->streaming_enabled) {
        audio_tee_src_pad_2 = gst_element_get_request_pad(data->audio_tee, "src_%u");
        video_tee_src_pad_2 = gst_element_get_request_pad(data->video_tee, "src_%u");
    }

    device_audio_queue_snk_pad = gst_element_get_static_pad(data->device_audio_queue, "sink");
    device_video_queue_snk_pad = gst_element_get_static_pad(data->device_video_queue, "sink");
    if (data->streaming_enabled) {
        stream_audio_queue_snk_pad = gst_element_get_static_pad(data->stream_audio_queue, "sink");
        stream_video_queue_snk_pad = gst_element_get_static_pad(data->stream_video_queue, "sink");
    }

    if (gst_pad_link(audio_tee_src_pad_1, device_audio_queue_snk_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(video_tee_src_pad_1, device_video_queue_snk_pad) != GST_PAD_LINK_OK) {
        g_printerr("Error: tee could not be linked with device sinks\n");
        result = 1;
        goto exit;
    }

    if (data->streaming_enabled) {
        if (gst_pad_link(audio_tee_src_pad_2, stream_audio_queue_snk_pad) != GST_PAD_LINK_OK ||
            gst_pad_link(video_tee_src_pad_2, stream_video_queue_snk_pad) != GST_PAD_LINK_OK) {
            g_printerr("Error: tee could not be linked with streaming sinks\n");
            result = 1;
            goto exit;
        }

        if (!gst_element_link_many(data->stream_audio_queue, data->voaacenc, data->flv_mux, NULL)) {
            g_printerr("Error: audio FLV elements could not be linked.\n");
            result = 1;
            goto exit;
        }

        if (!gst_element_link_many(data->stream_video_queue, data->x264enc, data->flv_mux, data->rtmp_sink, NULL)) {
            g_printerr("Error: video FLV elements could not be linked.\n");
            result = 1;
            goto exit;
        }
    }

    if (!gst_element_link_many(data->device_audio_queue, data->audio_device_sink, NULL)) {
        g_printerr("Error: device audio elements could not be linked.\n");
        result = 1;
        goto exit;
    }

    if (!gst_element_link_many(data->device_video_queue, data->video_device_sink, NULL)) {
        g_printerr("Error: device video elements could not be linked.\n");
        result = 1;
        goto exit;
    }

    // Connect to the pad-added signal
    for (i = 0; i < MAX_SOURCES; ++i) {
        g_signal_connect(data->source[i], "pad-added", G_CALLBACK(pad_added_handler), data);
    }

    // Clean resources
exit:
    if (audio_tee_src_pad_1) {
        gst_object_unref(audio_tee_src_pad_1);
    }
    if (audio_tee_src_pad_2) {
        gst_object_unref(audio_tee_src_pad_2);
    }
    if (video_tee_src_pad_1) {
        gst_object_unref(video_tee_src_pad_1);
    }
    if (video_tee_src_pad_2) {
        gst_object_unref(video_tee_src_pad_2);
    }
    if (stream_audio_queue_snk_pad) {
        gst_object_unref(stream_audio_queue_snk_pad);
    }
    if (device_audio_queue_snk_pad) {
        gst_object_unref(device_audio_queue_snk_pad);
    }
    if (stream_video_queue_snk_pad) {
        gst_object_unref(stream_video_queue_snk_pad);
    }
    if (device_video_queue_snk_pad) {
        gst_object_unref(device_video_queue_snk_pad);
    }

    return result;
}

static int create_pipeline(ApplicationContext* data) {
    if (create_pipeline_elements(data) != 0) {
        g_printerr("Error: failed to create pipeline elements\n");
        return 1;
    }

    if (add_elements_to_pipeline(data) != 0) {
        g_printerr("Error: failed to add elements to pipeline\n");
        return 1;
    }

    if (setup_videomixer_layout(data) != 0) {
        g_printerr("Error: failed to setup videomixer layout\n");
        return 1;
    }

    if (link_pipeline_elements(data) != 0) {
        g_printerr("Error: failed to link pipeline\n");
        return 1;
    }

    return 0;
}

static int run_pipeline(ApplicationContext* data) {
    GstBus* bus;
    GstMessage* msg;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;

    // Start playing
    ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Error: unable to set the pipeline to the playing state.\n");
        return 1;
    }

    // Listen to the bus
    bus = gst_element_get_bus(data->pipeline);
    do {
        msg = gst_bus_timed_pop_filtered(
            bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        /* Parse message */
        if (msg != NULL) {
            GError* err;
            gchar* debug_info;

            switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End-Of-Stream reached.\n");
                terminate = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                /* We are only interested in state-changed messages from the pipeline */
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline)) {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    g_print("Pipeline state changed from %s to %s:\n",
                            gst_element_state_get_name(old_state),
                            gst_element_state_get_name(new_state));
                }
                break;
            default:
                /* We should not reach here */
                g_printerr("Error: unexpected message received.\n");
                break;
            }
            gst_message_unref(msg);
        }
    } while (!terminate);

    gst_object_unref(bus);

    return 0;
}

static void free_resources(ApplicationContext* data) {
    if (data->pipeline) {
        gst_element_set_state(data->pipeline, GST_STATE_NULL);
        gst_object_unref(data->pipeline);
    }
}

// This function will be called by the pad-added signal
static void pad_added_handler(GstElement* src, GstPad* new_pad, ApplicationContext* data) {
    GstPad* sink_pad = NULL;
    GstPadLinkReturn ret;
    GstCaps* new_pad_caps = NULL;
    GstStructure* new_pad_struct = NULL;
    const gchar* new_pad_type = NULL;
    int i;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    // Check the new pad's type
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (g_str_has_prefix(new_pad_type, "audio/x-raw")) {
        // This is linear search but in case of small number of streams impact is not noticeable.
        // Need to search a source because we cannot imply the order in which this callback is called.
        for (i = 0; i < MAX_SOURCES; ++i) {
            if (data->source[i] == src) {
                if (i == AUDIO_FROM_SOURCE_INDEX) {
                    sink_pad = gst_element_get_static_pad(data->audio_convert, "sink");
                } else {
                    sink_pad = gst_element_get_static_pad(data->audio_sink[i], "sink");
                }
                break;
            }
        }
    } else if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
        // Read comment above about this search approach
        for (i = 0; i < MAX_SOURCES; ++i) {
            if (data->source[i] == src) {
                sink_pad = gst_element_get_static_pad(data->video_scale[i], "sink");
                break;
            }
        }
    }

    if (!sink_pad) {
        g_printerr("Error: failed to get sink pad in pad_added_handler");
        goto exit;
    }

    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("Type is '%s' but link failed.\n", new_pad_type);
    } else {
        g_print("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    if (sink_pad) {
        gst_object_unref(sink_pad);
    }

    if (new_pad_caps != NULL) {
        gst_caps_unref(new_pad_caps);
    }
}
#pragma once
#include "stdafx.h"
#include "pulse.h"

#include <sstream>

// name of our output, whereever that might come in handy
#define OUTPUT_NAME "PulseAudio"
// and the name of, err, foobar2000
#define APPLICATION_NAME "foobar2000"

// external functions from pulseaudio dll
static pa_bytes_to_usec                     g_pa_bytes_to_usec;
static pa_channel_map_init_auto             g_pa_channel_map_init_auto;
static pa_context_connect                   g_pa_context_connect;
static pa_context_disconnect                g_pa_context_disconnect;
static pa_context_errno                     g_pa_context_errno;
static pa_context_get_sink_input_info       g_pa_context_get_sink_input_info;
static pa_context_get_state                 g_pa_context_get_state;
static pa_context_new_with_proplist         g_pa_context_new_with_proplist;
static pa_context_set_event_callback        g_pa_context_set_event_callback;
static pa_context_set_sink_input_volume     g_pa_context_set_sink_input_volume;
static pa_context_set_state_callback        g_pa_context_set_state_callback;
static pa_context_set_subscribe_callback    g_pa_context_set_subscribe_callback;
static pa_context_subscribe                 g_pa_context_subscribe;
static pa_context_unref                     g_pa_context_unref;
static pa_cvolume_equal                     g_pa_cvolume_equal;
static pa_cvolume_init                      g_pa_cvolume_init;
static pa_cvolume_set                       g_pa_cvolume_set;
static pa_cvolume_valid                     g_pa_cvolume_valid;
static pa_operation_get_state               g_pa_operation_get_state;
static pa_operation_unref                   g_pa_operation_unref;
static pa_proplist_free                     g_pa_proplist_free;
static pa_proplist_new                      g_pa_proplist_new;
static pa_proplist_set                      g_pa_proplist_set;
static pa_proplist_setp                     g_pa_proplist_setp;
static pa_proplist_sets                     g_pa_proplist_sets;
static pa_stream_cancel_write               g_pa_stream_cancel_write;
static pa_stream_connect_playback           g_pa_stream_connect_playback;
static pa_stream_cork                       g_pa_stream_cork;
static pa_stream_disconnect                 g_pa_stream_disconnect;
static pa_stream_drain                      g_pa_stream_drain;
static pa_stream_drop                       g_pa_stream_drop;
static pa_stream_flush                      g_pa_stream_flush;
static pa_stream_get_buffer_attr            g_pa_stream_get_buffer_attr;
static pa_stream_get_index                  g_pa_stream_get_index;
static pa_stream_get_latency                g_pa_stream_get_latency;
static pa_stream_get_sample_spec            g_pa_stream_get_sample_spec;
static pa_stream_get_state                  g_pa_stream_get_state;
static pa_stream_get_timing_info            g_pa_stream_get_timing_info;
static pa_stream_is_corked                  g_pa_stream_is_corked;
static pa_stream_new                        g_pa_stream_new;
static pa_stream_set_started_callback       g_pa_stream_set_started_callback;
static pa_stream_set_state_callback         g_pa_stream_set_state_callback;
static pa_stream_set_underflow_callback     g_pa_stream_set_underflow_callback;
static pa_stream_set_write_callback         g_pa_stream_set_write_callback;
static pa_stream_trigger                    g_pa_stream_trigger;
static pa_stream_unref                      g_pa_stream_unref;
static pa_stream_update_sample_rate         g_pa_stream_update_sample_rate;
static pa_stream_update_timing_info         g_pa_stream_update_timing_info;
static pa_stream_writable_size              g_pa_stream_writable_size;
static pa_stream_write                      g_pa_stream_write;
static pa_strerror                          g_pa_strerror;
static pa_sw_volume_from_dB                 g_pa_sw_volume_from_dB;
static pa_sw_volume_to_dB                   g_pa_sw_volume_to_dB;
static pa_threaded_mainloop_accept          g_pa_threaded_mainloop_accept;
static pa_threaded_mainloop_free            g_pa_threaded_mainloop_free;
static pa_threaded_mainloop_get_api         g_pa_threaded_mainloop_get_api;
static pa_threaded_mainloop_get_retval      g_pa_threaded_mainloop_get_retval;
static pa_threaded_mainloop_lock            g_pa_threaded_mainloop_lock;
static pa_threaded_mainloop_new             g_pa_threaded_mainloop_new;
static pa_threaded_mainloop_signal          g_pa_threaded_mainloop_signal;
static pa_threaded_mainloop_start           g_pa_threaded_mainloop_start;
static pa_threaded_mainloop_stop            g_pa_threaded_mainloop_stop;
static pa_threaded_mainloop_unlock          g_pa_threaded_mainloop_unlock;
static pa_threaded_mainloop_wait            g_pa_threaded_mainloop_wait;
static pa_usec_to_bytes                     g_pa_usec_to_bytes;

// used to mark whether pulseaudio dll was loaded successfully
static bool g_pa_is_loaded = false;

// component setting identifiers I guess
static const GUID guid_cfg_pulseaudio_branch            = {0x61979096, 0x1158, 0x4860, {0xb0, 0xcc, 0x6f, 0x53, 0x0f, 0x35, 0xaf, 0x26} };
static const GUID guid_cfg_pulseaudio_fade_out_seek     = {0x319d2507, 0xe2aa, 0x40e2, {0xa1, 0xec, 0x4e, 0x94, 0xf1, 0xdd, 0x12, 0x8a} };
static const GUID guid_cfg_pulseaudio_fade_in_seek      = {0x90ae1a07, 0xcd2b, 0x481c, {0xb2, 0x6a, 0xf7, 0x36, 0x83, 0xec, 0xf6, 0x40} };
static const GUID guid_cfg_pulseaudio_fade_out_track    = {0xe136e959, 0x929b, 0x4005, {0xaa, 0x9e, 0x8e, 0x8b, 0x91, 0x5b, 0x5d, 0x02} };
static const GUID guid_cfg_pulseaudio_fade_in_track     = {0x06fb3670, 0x4e7d, 0x4601, {0x83, 0xa6, 0xed, 0x44, 0x3e, 0xb1, 0xe1, 0x07} };
static const GUID guid_cfg_pulseaudio_minreq_workaround = {0xe176bd02, 0x0cbc, 0x4fbd, {0x8f, 0x1a, 0xf2, 0x3a, 0x2a, 0xb7, 0x08, 0x86} };
static const GUID guid_cfg_pulseaudio_prebuffer         = {0x64cd1e28, 0x87ea, 0x41e5, {0xaf, 0x3d, 0xc6, 0xcd, 0x2f, 0x52, 0xac, 0xee} };
// moved here from g_enum_devices, I guess it's used for getting the pulseaudio output "device" to foobar2000
static const GUID guid_cfg_pulseaudio_device            = {0x08bf1c19, 0x5b9d, 0x4992, {0x76, 0x18, 0x13, 0x8b, 0xa2, 0x01, 0xd7, 0xa6} };
static const GUID guid_cfg_pulseaudio_server            = {0xbf045193, 0xde9b, 0x432d, {0xa5, 0xd9, 0x36, 0xb1, 0x19, 0x57, 0x6a, 0x61} };

// and the actual settings under advanced settings
static advconfig_branch_factory g_pulseaudio_output_branch(OUTPUT_NAME " output",
    guid_cfg_pulseaudio_branch, advconfig_branch::guid_branch_playback, 0);
static advconfig_integer_factory cfg_pulseaudio_seek_fade_out("Fade out on seek (msec)",
    guid_cfg_pulseaudio_fade_out_seek, guid_cfg_pulseaudio_branch, 0, 15, 0, 1000, 0);
static advconfig_integer_factory cfg_pulseaudio_seek_fade_in("Fade in on seek (msec)",
    guid_cfg_pulseaudio_fade_in_seek, guid_cfg_pulseaudio_branch, 0, 15, 0, 1000, 0);
static advconfig_integer_factory cfg_pulseaudio_track_fade_out("Fade out on manual track change (msec)",
    guid_cfg_pulseaudio_fade_out_track, guid_cfg_pulseaudio_branch, 0, 0, 0, 1000, 0);
static advconfig_integer_factory cfg_pulseaudio_track_fade_in("Fade in on manual track change (msec)",
    guid_cfg_pulseaudio_fade_in_track, guid_cfg_pulseaudio_branch, 0, 0, 0, 1000, 0);
// TODO: what is this
static advconfig_checkbox_factory cfg_pulseaudio_minreq_workaround("Enable workaround for driver issue",
    guid_cfg_pulseaudio_minreq_workaround, guid_cfg_pulseaudio_branch, 0, false);
static advconfig_integer_factory cfg_pulseaudio_prebuf("Request prebuffer (milliseconds)",
    guid_cfg_pulseaudio_prebuffer, guid_cfg_pulseaudio_branch, 0, 200, 0, 100000);
// mt variant allows reading the value from worker threads, which seems to be what we need
static advconfig_string_factory_MT cfg_pulseaudio_server(OUTPUT_NAME " server",
    guid_cfg_pulseaudio_server, guid_cfg_pulseaudio_branch, 0, "tcp4:127.0.0.1");

class output_pulse : public output_v4 {
public:
    typedef struct fade_in {
        bool active = false;
        size_t total_samples;
        size_t progress;
    } fade;

    output_pulse(const GUID&, double, bool, t_uint32);
    ~output_pulse();

    void pause(bool);
    void volume_set(double);
    void flush();
    void flush_changing_track();
    void update(bool&);
    size_t update_v2();
    void force_play();
    double get_latency();

    void process_samples(const audio_chunk&);
    bool is_progressing();
    pfc::eventHandle_t get_trigger_event();
    static void g_enum_devices(output_device_enum_callback&);

    static bool g_advanced_settings_query();
    static bool g_needs_bitdepth_config();
    static bool g_needs_dither_config();
    static bool g_needs_device_list_prefixes();
    static bool g_supports_multiple_streams();
    static bool g_is_high_latency();
    static uint32_t g_extra_flags();
    static void g_advanced_settings_popup(HWND, POINT);
    static const char* g_get_name();
    static GUID g_get_guid();

private:
    const double offset = 0.05;
    pa_context* context = NULL;
    pa_threaded_mainloop* mainloop = NULL;
    pa_stream* stream = NULL;
    pfc::array_t<audio_sample, pfc::alloc_fast_aggressive> m_incoming;
    size_t m_incoming_ptr;
    t_samplespec m_incoming_spec, m_active_spec;
    double buffer_length;
    pa_volume_t volume;
    bool progressing;
    bool draining;
    bool drained;
    bool rewind_active;
    lookback_buffer rewind_buffer;
    const size_t cfg_seek_fade_in;
    const size_t cfg_seek_fade_out;
    const size_t cfg_track_fade_in;
    const size_t cfg_track_fade_out;
    size_t fade_in_next_ms;
    fade active_fade_in;
    bool next_write_relative;
    pfc::event trigger_update;
    service_ptr_t<playback_control> playback_control;

    // no idea
    static bool context_wait(pa_context*, pa_threaded_mainloop*);
    static void context_subscribe_cb(pa_context*, pa_subscription_event_type_t, uint32_t, void*);
    static void sink_input_info_cb(pa_context*, const pa_sink_input_info*, int, void*);
    // stops playback
    static void stop();
    // no idea what
    static void context_state_cb(pa_context*, void*);
    // stream methods and callbacks
    static int stream_wait(pa_stream*, pa_threaded_mainloop*);
    static void stream_state_cb(pa_stream*, void*);
    static void stream_started_cb(pa_stream*, void*);
    static void stream_underflow_cb(pa_stream*, void*);
    static void stream_write_cb(pa_stream*, size_t, void*);
    // writes stuff to pulseaudio stream
    size_t write();
    // total mystery method
    void fade_section(audio_sample*, size_t, size_t, size_t, size_t, bool);
    // writes fadeout to the stream
    void write_fade_out(size_t);
    // signals something to the pulseaudio mainloop
    static void stream_success_cb(pa_stream*, int, void*);
    // waits until the pulseaudio operation has been completed
    void wait_for_op(pa_operation*);
    // closes the pulseaudio stream
    void close_stream();
    // opens a pulseaudio stream for the incoming spec
    void open_incoming_spec();
    // wrapper for logging errors into the console
    static void console_error(const char*, int);
    // callback for something
    static void stream_drained_cb(pa_stream*, int, void*);
    // loads external functions from libpulse-0.dll
    static bool load_pulse_dll();
};

// needs to reside where the class definition is
static output_factory_t<output_pulse> g_output_pulse_factory;

#pragma once
#include "stdafx.h"
#include <sstream>
#include <pathcch.h>
#include <windows.h>
#include <mutex>

#include "core_api.h"
#include "output.h"

// name of our output, whereever that might come in handy
#define OUTPUT_NAME "PulseAudio"
// and the name of, err, foobar2000
#define APPLICATION_NAME "foobar2000"
// name of the libpulse-0.dll to load, without any paths
#define LIBPULSE_DLL = "libpulse-0.dll"

// used to mark whether pulseaudio dll was loaded successfully
static bool g_pa_is_loaded = false;

// component setting identifiers I guess
static const GUID guid_cfg_pulseaudio_branch            = {0x61979096, 0x1158, 0x4860, {0xb0, 0xcc, 0x6f, 0x53, 0x0f, 0x35, 0xaf, 0x26} };
static const GUID guid_cfg_pulseaudio_prebuffer         = {0x64cd1e28, 0x87ea, 0x41e5, {0xaf, 0x3d, 0xc6, 0xcd, 0x2f, 0x52, 0xac, 0xee} };
// moved here from g_enum_devices, I guess it's used for getting the pulseaudio output "device" to foobar2000
static const GUID guid_cfg_pulseaudio_device            = {0x08bf1c19, 0x5b9d, 0x4992, {0x76, 0x18, 0x13, 0x8b, 0xa2, 0x01, 0xd7, 0xa6} };
static const GUID guid_cfg_pulseaudio_server            = {0xbf045193, 0xde9b, 0x432d, {0xa5, 0xd9, 0x36, 0xb1, 0x19, 0x57, 0x6a, 0x61} };
// moved here from g_get_guid, seems to be the guid of the component
static const GUID guid_cfg_pulseaudio_output            = {0x0fe94df9, 0xc8e2, 0x40a1, {0x40, 0xa1, 0xb1, 0x2a, 0x4a, 0x6c, 0xe4, 0x9e} };

// and the actual settings under advanced settings
static advconfig_branch_factory g_pulseaudio_output_branch(OUTPUT_NAME " output",
    guid_cfg_pulseaudio_branch, advconfig_branch::guid_branch_playback, 0);
static advconfig_integer_factory cfg_pulseaudio_prebuf("Request prebuffer (milliseconds)",
    guid_cfg_pulseaudio_prebuffer, guid_cfg_pulseaudio_branch, 0, 200, 0, 100000);
// mt variant allows reading the value from worker threads, which seems to be what we need
static advconfig_string_factory_MT cfg_pulseaudio_server(OUTPUT_NAME " server",
    guid_cfg_pulseaudio_server, guid_cfg_pulseaudio_branch, 0, "tcp4:127.0.0.1");

class output_pulse : public output_v4
{
public:
    output_pulse(const GUID&, double, bool, t_uint32);
    ~output_pulse();

    void volume_set(double);
    // "Called after seeking"
    void flush();
    // bool "receives value indicating whether the device is ready for next process_samples() call"
    void update(bool&);
    // "returns 0 if the output isn't ready to receive any new data, otherwise an advisory
    // number of samples - at the current stream format - that the output expects to take now"
    size_t update_v2();
    // "Called when there's no more data to send, to prevent infinite waiting"
    void force_play();
    // "Sends new samples to the device. Allowed to be called only when update() indicates that the device is ready."
    void process_samples(const audio_chunk&);

    // whether the audio stream is being played or not, defined in output
    bool is_progressing() { return progressing; }

    // seconds of audio data queued for playback, defined in output
    double get_latency();

    // pauses (true) or resumes (false) the stream depending on the parameter
    void pause(bool);

    // output_entry method definition; enumerates pulseaudio devices
    static void g_enum_devices(output_device_enum_callback&);

    // additional output_entry method definitions
    static GUID g_get_guid() { return guid_cfg_pulseaudio_output; }
    static const char* g_get_name() { return OUTPUT_NAME; }
    static void g_advanced_settings_popup(HWND, POINT) {}
    static bool g_advanced_settings_query() { return false; }
    static bool g_needs_bitdepth_config() { return false; }
    static bool g_needs_dither_config() { return false; }
    static bool g_needs_device_list_prefixes() { return true; }
    static bool g_supports_multiple_streams() { return false; }
    static bool g_is_high_latency() { return true; }

private:
    // incoming samples and specs as specified in output.h
    pfc::array_t<audio_sample, pfc::alloc_fast_aggressive> m_incoming;
    size_t m_incoming_ptr;
    t_samplespec m_incoming_spec, m_active_spec;

    // pulseaudio member variables
    pa_context* context;
    pa_stream* stream;
    pa_threaded_mainloop* mainloop;

    // context callbacks
    static void context_state_cb(pa_context*, void*);

    static void context_subscribe_cb(pa_context*, pa_subscription_event_type_t, uint32_t, void*);

    // stream callbacks; define simple ones simply here
    static void stream_drained_cb(pa_stream*, int, void*);

    static void stream_started_cb(pa_stream* p, void* userdata)
    {
        ((output_pulse*)userdata)->progressing = true;
    }

    static void stream_state_cb(pa_stream*, void*);

    static void stream_success_cb(pa_stream* s, int success, void* userdata)
    {
        g_pa_threaded_mainloop_signal((pa_threaded_mainloop*)userdata, 0);
    }

    static void stream_underflow_cb(pa_stream*, void*);

    static void stream_write_cb(pa_stream* p, size_t nbytes, void* userdata)
    {
        ((output_pulse*)userdata)->trigger_update.set_state(true);
    }

    // TODO: seems to be used just in set_volume
    static void sink_input_info_cb(pa_context*, const pa_sink_input_info*, int, void*);



    // stops playback, used only ever in error situations
    static void stop()
    {
        fb2k::inMainThread([]()
        {
            playback_control::get()->stop();
        });
    }


    // is the audio stream being played or not, read by is_progressing
    bool progressing;

    // is the audio stream being drained or already drained
    bool draining;
    bool drained;


    // wrapper for connecting the pulseaudio context, returns true on success
    bool context_connect();

    // wrapper for connecting the pulseaudio stream, returns true on success
    bool stream_connect(const pa_sample_spec*, const pa_buffer_attr*);


    // indicates whether we are seeking or not
    bool next_write_relative;

    const double offset = 0.05;
    double buffer_length;
    pa_volume_t volume;
    pfc::event trigger_update;
    service_ptr_t<playback_control> playback_control;

    // writes stuff to pulseaudio stream
    size_t write();
    // closes the pulseaudio stream
    void close_stream();
    // opens a pulseaudio stream for the incoming spec
    void open_incoming_spec();

    static bool load_pulse_dll();
     
    // logging functions
    enum Severity
    {
        Error,
        Info,
    };

    static void pa_console_error(const char*, int);

    static void console_message(Severity, const char*, va_list);

    static void console_error(const char*, ...);

    static void console_info(const char*, ...);
};

// needs to reside where the class definition is
static output_factory_t<output_pulse> g_output_pulse_factory;

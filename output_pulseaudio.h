#pragma once
#include "stdafx.h"
#include <sstream>
#include <pathcch.h>
#include <windows.h>
#include <mutex>
#include <filesystem>

#include "advconfig_impl.h"
#include "core_api.h"
#include "output.h"
#include "playback_control.h"

#if _WIN64
#define PLATFORM "Win64"
#else
#define PLATFORM "Win32"
#endif

#define APPLICATION_NAME "foobar2000"
#define APPLICATION_ID APPLICATION_NAME
#define APPLICATION_ICON_NAME APPLICATION_NAME
#define OUTPUT_NAME "PulseAudio"

static const GUID guid_cfg_pulseaudio_branch
    = {0x61979096, 0x1158, 0x4860, {0xb0, 0xcc, 0x6f, 0x53, 0x0f, 0x35, 0xaf, 0x26} };
static const GUID guid_cfg_pulseaudio_device
    = {0x08bf1c19, 0x5b9d, 0x4992, {0x76, 0x18, 0x13, 0x8b, 0xa2, 0x01, 0xd7, 0xa6} };
static const GUID guid_cfg_pulseaudio_server
    = {0xbf045193, 0xde9b, 0x432d, {0xa5, 0xd9, 0x36, 0xb1, 0x19, 0x57, 0x6a, 0x61} };
static const GUID guid_cfg_pulseaudio_output
    = {0x0fe94df9, 0xc8e2, 0x40a1, {0x40, 0xa1, 0xb1, 0x2a, 0x4a, 0x6c, 0xe4, 0x9e} };

static advconfig_branch_factory g_pulseaudio_output_branch(OUTPUT_NAME " output",
    guid_cfg_pulseaudio_branch, advconfig_branch::guid_branch_playback, 0);
static advconfig_string_factory_MT cfg_pulseaudio_server("Server",
    guid_cfg_pulseaudio_server, guid_cfg_pulseaudio_branch, 0, "tcp4:127.0.0.1");

class output_pulse : public output_v8
{
public:
    output_pulse(const GUID&, double, bool, t_uint32);
    ~output_pulse();

    void on_update() { /* No-op for now */ }
    void write(const audio_chunk& p_data);
    t_size can_write_samples();
    t_size get_latency_samples();
    //struct latencySamples_t { size_t soft, hard; }; // TODO
    //latencySamples_t get_latency_samples_v2(); // TODO
    void on_flush();
    void open(audio_chunk::spec_t const& p_spec);
    void on_force_play();
    void pause(bool p_state);
    void volume_set(double p_val) { /* Zero use for this, maybe reimplement later */ }

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
    pa_context* context;
    pa_stream* stream;
    pa_threaded_mainloop* mainloop;
    bool draining;
    bool drained;
    bool next_write_relative;
    double buffer_length;

    static void context_state_cb(pa_context* ctx, void* userdata)
    {
        pa_threaded_mainloop* ml = (pa_threaded_mainloop*)userdata;
        switch (g_pa_context_get_state(ctx))
        {
        case PA_CONTEXT_FAILED:
            console::error("PA_CONTEXT_FAILED");
            stop();
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
            g_pa_threaded_mainloop_signal(ml, 0);
        }
    }

    static void stream_drained_cb(pa_stream* s, int success, void* userdata)
    {
        output_pulse* o = (output_pulse*)userdata;
        o->draining = false;
        o->drained = true;
    }

    static void stream_state_cb(pa_stream* s, void* userdata)
    {
        pa_threaded_mainloop* ml = (pa_threaded_mainloop*)userdata;
        switch (g_pa_stream_get_state(s))
        {
        case PA_STREAM_FAILED:
            console::error("PA_STREAM_FAILED");
        case PA_STREAM_READY:
        case PA_STREAM_TERMINATED:
            g_pa_threaded_mainloop_signal(ml, 0);
        }
    }

    static void stream_success_cb(pa_stream* s, int success, void* userdata)
    {
        pa_threaded_mainloop* ml = (pa_threaded_mainloop*)userdata;
        g_pa_threaded_mainloop_signal(ml, 0);
    }

    static void stop() {
        service_ptr_t<playback_control> playback_control;
        fb2k::inMainThread([]() {
            playback_control::get()->stop();
        });
    }

    bool stream_connect(const pa_sample_spec*, const pa_buffer_attr*);

    static bool load_pulse_dll();

    latencyInfo_t get_latency_info();

    // See output_impl.cpp for the stuff below
    void flush();
    void update(bool& p_ready);
    size_t update_v2();
    void process_samples(const audio_chunk& p_chunk);
    size_t process_samples_v2(const audio_chunk&);
    void force_play();
    void on_flush_internal();
    void send_force_play();

    bool queue_empty() const { return m_incoming_ptr == m_incoming.get_size(); }

    pfc::array_t<audio_sample, pfc::alloc_fast_aggressive> m_incoming;
    size_t m_incoming_ptr = 0, m_can_write = 0;
    audio_chunk::spec_t m_incoming_spec, m_active_spec;
    bool m_eos = false; // EOS issued by caller / no more data expected until a flush
    bool m_sent_force_play = false; // set if sent on_force_play()
};

static output_factory_t<output_pulse> g_output_pulse_factory;

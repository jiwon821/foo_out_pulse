#pragma once

#include "pulse.h"

namespace {
    // seems to be unused
    //typedef HRESULT(CALLBACK* LPFNDLLFUNC1)(DWORD, UINT*);

    // external functions from pulseaudio dll
    static pa_strerror g_pa_strerror;
    static pa_threaded_mainloop_new g_pa_threaded_mainloop_new;
    static pa_threaded_mainloop_free g_pa_threaded_mainloop_free;
    static pa_threaded_mainloop_start g_pa_threaded_mainloop_start;
    static pa_threaded_mainloop_stop g_pa_threaded_mainloop_stop;
    static pa_threaded_mainloop_lock g_pa_threaded_mainloop_lock;
    static pa_threaded_mainloop_unlock g_pa_threaded_mainloop_unlock;
    static pa_threaded_mainloop_wait g_pa_threaded_mainloop_wait;
    static pa_threaded_mainloop_signal g_pa_threaded_mainloop_signal;
    static pa_threaded_mainloop_accept g_pa_threaded_mainloop_accept;
    static pa_threaded_mainloop_get_retval g_pa_threaded_mainloop_get_retval;
    static pa_threaded_mainloop_get_api g_pa_threaded_mainloop_get_api;
    static pa_stream_new g_pa_stream_new;
    static pa_stream_connect_playback g_pa_stream_connect_playback;
    static pa_stream_disconnect g_pa_stream_disconnect;
    static pa_stream_unref g_pa_stream_unref;
    static pa_stream_write g_pa_stream_write;
    static pa_stream_cancel_write g_pa_stream_cancel_write;
    static pa_stream_drop g_pa_stream_drop;
    static pa_stream_writable_size g_pa_stream_writable_size;
    static pa_stream_drain g_pa_stream_drain;
    static pa_stream_set_write_callback g_pa_stream_set_write_callback;
    static pa_stream_set_state_callback g_pa_stream_set_state_callback;
    static pa_stream_set_started_callback g_pa_stream_set_started_callback;
    static pa_stream_set_underflow_callback g_pa_stream_set_underflow_callback;
    static pa_stream_cork g_pa_stream_cork;
    static pa_stream_is_corked g_pa_stream_is_corked;
    static pa_stream_flush g_pa_stream_flush;
    static pa_stream_update_sample_rate g_pa_stream_update_sample_rate;
    static pa_stream_get_state g_pa_stream_get_state;
    static pa_stream_get_sample_spec g_pa_stream_get_sample_spec;
    static pa_stream_get_latency g_pa_stream_get_latency;
    static pa_stream_get_timing_info g_pa_stream_get_timing_info;
    static pa_stream_trigger g_pa_stream_trigger;
    static pa_stream_update_timing_info g_pa_stream_update_timing_info;
    static pa_stream_get_buffer_attr g_pa_stream_get_buffer_attr;
    static pa_proplist_new g_pa_proplist_new;
    static pa_proplist_free g_pa_proplist_free;
    static pa_proplist_set g_pa_proplist_set;
    static pa_proplist_sets g_pa_proplist_sets;
    static pa_proplist_setp g_pa_proplist_setp;
    static pa_context_new_with_proplist g_pa_context_new_with_proplist;
    static pa_context_unref g_pa_context_unref;
    static pa_context_errno g_pa_context_errno;
    static pa_context_connect g_pa_context_connect;
    static pa_context_disconnect g_pa_context_disconnect;
    static pa_context_get_state g_pa_context_get_state;
    static pa_context_set_state_callback g_pa_context_set_state_callback;
    static pa_context_set_event_callback g_pa_context_set_event_callback;
    static pa_operation_unref g_pa_operation_unref;
    static pa_operation_get_state g_pa_operation_get_state;
    static pa_bytes_to_usec g_pa_bytes_to_usec;
    static pa_usec_to_bytes g_pa_usec_to_bytes;
    static pa_channel_map_init_auto g_pa_channel_map_init_auto;
    static pa_stream_get_index g_pa_stream_get_index;
    static pa_sw_volume_from_dB g_pa_sw_volume_from_dB;
    static pa_sw_volume_to_dB g_pa_sw_volume_to_dB;
    static pa_cvolume_valid g_pa_cvolume_valid;
    static pa_cvolume_equal g_pa_cvolume_equal;
    static pa_context_set_sink_input_volume g_pa_context_set_sink_input_volume;
    static pa_cvolume_init g_pa_cvolume_init;
    static pa_cvolume_set g_pa_cvolume_set;
    static pa_context_get_sink_input_info g_pa_context_get_sink_input_info;
    static pa_context_subscribe g_pa_context_subscribe;
    static pa_context_set_subscribe_callback g_pa_context_set_subscribe_callback;

    // used to mark whether pulseaudio dll was loaded successfully
    static bool g_pa_is_loaded = false;

    // component setting identifiers I guess
    static const GUID guid_cfg_pulseaudio_branch = {0x61979096, 0x1158, 0x4860, {0xb0, 0xcc, 0x6f, 0x53, 0xf, 0x35, 0xaf, 0x26} };
    static const GUID guid_cfg_pulseaudio_fade_out_seek = {0x319d2507, 0xe2aa, 0x40e2, {0xa1, 0xec, 0x4e, 0x94, 0xf1, 0xdd, 0x12, 0x8a} };
    static const GUID guid_cfg_pulseaudio_fade_in_seek = {0x90ae1a07, 0xcd2b, 0x481c, {0xb2, 0x6a, 0xf7, 0x36, 0x83, 0xec, 0xf6, 0x40} };
    static const GUID guid_cfg_pulseaudio_fade_out_track = {0xe136e959, 0x929b, 0x4005, {0xaa, 0x9e, 0x8e, 0x8b, 0x91, 0x5b, 0x5d, 0x2} };
    static const GUID guid_cfg_pulseaudio_fade_in_track = {0x6fb3670, 0x4e7d, 0x4601, {0x83, 0xa6, 0xed, 0x44, 0x3e, 0xb1, 0xe1, 0x7} };
    static const GUID guid_cfg_pulseaudio_minreq_workaround = {0xe176bd02, 0xcbc, 0x4fbd, {0x8f, 0x1a, 0xf2, 0x3a, 0x2a, 0xb7, 0x8, 0x86} };
    // seems to be unused
    //static const GUID guid_cfg_pulseaudio_fade_out_stop = {0xbf045192, 0xde9b, 0x432d, {0xa5, 0xd9, 0x36, 0xb1, 0x19, 0x57, 0x6a, 0x61} };
    static const GUID guid_cfg_pulseaudio_prebuffer = {0x64cd1e28, 0x87ea, 0x41e5, {0xaf, 0x3d, 0xc6, 0xcd, 0x2f, 0x52, 0xac, 0xee} };

    // and the actual settings under advanced settings
    static advconfig_branch_factory g_pulseaudio_output_branch("Pulseaudio output", guid_cfg_pulseaudio_branch, advconfig_branch::guid_branch_playback, 0);
    static advconfig_integer_factory cfg_pulseaudio_seek_fade_out("Fade out on seek (milliseconds)", guid_cfg_pulseaudio_fade_out_seek, guid_cfg_pulseaudio_branch, 0, 15, 0, 1000, 0);
    static advconfig_integer_factory cfg_pulseaudio_seek_fade_in("Fade in on seek (milliseconds)", guid_cfg_pulseaudio_fade_in_seek, guid_cfg_pulseaudio_branch, 0, 15, 0, 1000, 0);
    static advconfig_integer_factory cfg_pulseaudio_track_fade_out("Fade out on manual track change (milliseconds)", guid_cfg_pulseaudio_fade_out_track, guid_cfg_pulseaudio_branch, 0, 0, 0, 1000, 0);
    static advconfig_integer_factory cfg_pulseaudio_track_fade_in("Fade in on manual track change (milliseconds)", guid_cfg_pulseaudio_fade_in_track, guid_cfg_pulseaudio_branch, 0, 0, 0, 1000, 0);
    static advconfig_checkbox_factory cfg_pulseaudio_minreq_workaround("Enable workaround for driver issue", guid_cfg_pulseaudio_minreq_workaround, guid_cfg_pulseaudio_branch, 0, false);
    static advconfig_integer_factory cfg_pulseaudio_prebuf("Request prebuffer (milliseconds)", guid_cfg_pulseaudio_prebuffer, guid_cfg_pulseaudio_branch, 0, 200, 0, 100000, 0);

    // seems to be unused
    //static output_factory_t<output_pulse> g_output_pulse_factory;
}
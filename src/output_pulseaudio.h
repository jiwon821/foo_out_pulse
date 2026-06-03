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

    // seems to be unused
    //static output_factory_t<output_pulse> g_output_pulse_factory;
}
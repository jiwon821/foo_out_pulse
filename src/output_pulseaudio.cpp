#include "stdafx.h"
#include "output_pulseaudio.h"

#include <pathcch.h>
#include <windows.h>

#include <mutex>
#include <sstream>

#include "core_api.h"
#include "output.h"

output_pulse::output_pulse(const GUID& p_device, double p_buffer_length, bool p_dither, t_uint32 p_bitdepth)
      : buffer_length(p_buffer_length),
        next_write_relative(false),
        volume(0)
{
    stream = NULL;
    context = NULL;
    mainloop = NULL;
    progressing = false;
    draining = false;
    drained = false;
    m_incoming_ptr = 0;

    if (!load_pulse_dll())
    {
        stop();
        return;
    }

    mainloop = g_pa_threaded_mainloop_new();
    if (g_pa_threaded_mainloop_start(mainloop) < 0)
    {
        g_pa_threaded_mainloop_free(mainloop);
        mainloop = NULL;
        console::error("Error starting playback thread");
        stop();
        return;
    }

    // just puts foobar2000 to everything
    pa_proplist* proplist = g_pa_proplist_new();
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, APPLICATION_NAME);
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, APPLICATION_NAME);
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, APPLICATION_NAME);

    pa_mainloop_api* api;
    g_pa_threaded_mainloop_lock(mainloop);
    api = g_pa_threaded_mainloop_get_api(mainloop);
    context = g_pa_context_new_with_proplist(api, "foobar2000", proplist);
    if (proplist)
    {
        g_pa_proplist_free(proplist);
    }

    // notifies context_state_cb when the server connection is established below
    g_pa_context_set_state_callback(context, context_state_cb, this);

    if (!context_connect())
    {
        g_pa_context_unref(context);
        context = NULL;
        g_pa_threaded_mainloop_unlock(mainloop);
        g_pa_threaded_mainloop_stop(mainloop);
        g_pa_threaded_mainloop_free(mainloop);
        mainloop = NULL;

        // full playback stop
        stop();
        return;
    }

    g_pa_threaded_mainloop_unlock(mainloop);

    trigger_update.create(true, true);
}

bool output_pulse::context_connect()
{
    pfc::string8 server_string;
    pa_context_state_t state;
    pa_operation *operation;

    // read server connection string from settings
    cfg_pulseaudio_server.get(server_string);

    // connect context to server, returns negative on certain errors: https://www.freedesktop.org/software/pulseaudio/doxygen/context_8h.html#a983ce13d45c5f4b0db8e1a34e21f9fce
    if (g_pa_context_connect(context, server_string, (pa_context_flags_t)0, NULL) < 0)
    {
        console::error("pa_context_connect failure");
        return false;
    }

    // wait until ready
    while ((state = g_pa_context_get_state(context)) != PA_CONTEXT_READY)
    {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
        {
            console::error("pa_context_get_state returned error code");
            return false;
        }

        g_pa_threaded_mainloop_wait(mainloop);
    }

    // subscribe to event notifications: https://www.freedesktop.org/software/pulseaudio/doxygen/subscribe_8h.html#abe684246fd5cb640b0199bcfe7f801b0
    if (operation = g_pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL))
    {
        g_pa_operation_unref(operation);
    }

    // call context_subscribe_callback on events: https://www.freedesktop.org/software/pulseaudio/doxygen/subscribe_8h.html#a55281f798863e7b37594d347be7ad98c
    g_pa_context_set_subscribe_callback(context, context_subscribe_cb, this);

    console::info("pa_context_connect success");
    return true;
}

output_pulse::~output_pulse()
{
    if (mainloop)
    {
        g_pa_threaded_mainloop_lock(mainloop);
        if (context)
        {
            g_pa_context_disconnect(context);
            g_pa_context_set_event_callback(context, NULL, NULL);
            g_pa_context_set_state_callback(context, NULL, NULL);
            g_pa_context_unref(context);
        }
        g_pa_threaded_mainloop_unlock(mainloop);

        g_pa_threaded_mainloop_stop(mainloop);
        Sleep(100);  // _stop() doesn't seem to block until it's actually safe to
                    // free the mainloop?
        g_pa_threaded_mainloop_free(mainloop);
    }
}

void output_pulse::pause(bool p_state)
{
    pa_operation *operation;

    if (stream)
    {
        g_pa_threaded_mainloop_lock(mainloop);

        // cork means pause in pulseaudio https://freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a14e698233ac2d246646651955ab0ec7b
        if (operation = g_pa_stream_cork(stream, p_state, NULL, NULL))
        {
            g_pa_operation_unref(operation);
        }

        g_pa_threaded_mainloop_unlock(mainloop);
    }
}

void output_pulse::volume_set(double p_val)
{
    if (!stream)
    {
        return;
    }

    pa_volume_t new_volume = g_pa_sw_volume_from_dB(p_val);
    if (new_volume != volume) {
    volume = new_volume;
    uint32_t index = g_pa_stream_get_index(stream);
    pa_cvolume cvolume;
    g_pa_cvolume_init(&cvolume);
    cvolume.channels = m_active_spec.m_channels;
    g_pa_cvolume_set(&cvolume, m_active_spec.m_channels, volume);

    g_pa_threaded_mainloop_lock(mainloop);
    pa_operation* op = g_pa_context_set_sink_input_volume(context, index, &cvolume, NULL, NULL);
    if (op)
    {
        g_pa_operation_unref(op);
    }
    g_pa_threaded_mainloop_unlock(mainloop);
    }
}

void output_pulse::flush()
{
    m_incoming_ptr = 0;
    m_incoming.set_size(0);

    // from write_fade_out in case these are needed
    next_write_relative = true;
    trigger_update.set_state(true);
}

void output_pulse::flush_changing_track()
{
    m_incoming_ptr = 0;
    m_incoming.set_size(0);

    // from write_fade_out in case these are needed
    next_write_relative = true;
    trigger_update.set_state(true);
}

void output_pulse::update(bool& p_ready)
{
    p_ready = update_v2() > 0;
}

size_t output_pulse::update_v2()
{
    trigger_update.set_state(false);

    if (m_incoming_spec != m_active_spec)
    {
        if (drained || next_write_relative)
        {
            next_write_relative = false;
            drained = false;
            open_incoming_spec();
        }
        else
        {
            force_play();
        }
    }

    size_t retCanWriteSamples = 0;
    if (m_incoming_spec == m_active_spec && m_incoming_ptr < m_incoming.get_size())
    {
        retCanWriteSamples = write();
    }
    else if (m_incoming_ptr == m_incoming.get_size())
    {
        retCanWriteSamples = SIZE_MAX;
    }
    return retCanWriteSamples;
}

void output_pulse::force_play()
{
    pa_operation *operation;

    if (draining)
    {
        // only one drain operation per stream may be issued at a time: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a8d263f188073f244b3820f3f50db4ba5
        return;
    }

    if (stream)
    {
        g_pa_threaded_mainloop_lock(mainloop);

        draining = true;
        drained = false;

        // drain the stream, notify after all audio has been played and playback buffer is empty
        if (operation = g_pa_stream_drain(stream, stream_drained_cb, this))
        {
            g_pa_operation_unref(operation);
        }
        else
        {
            // nothing to drain
            draining = false;
            drained = true;
        }

        // immediately start playback on the stream: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#ae17a3a9f6ee0403c4665f6f4ce02ca3c
        if (operation = g_pa_stream_trigger(stream, NULL, NULL))
        {
            g_pa_operation_unref(operation);
        }

        g_pa_threaded_mainloop_unlock(mainloop);
    }
    else
    {
        // no stream, so supposedly we must be drained
        draining = false;
        drained = true;
    }
}

double output_pulse::get_latency()
{
    double latency_sec = 0;
    size_t samples;
    pa_usec_t latency_usec;
    const pa_timing_info *timing_info;
    pa_operation *operation;

    if (m_incoming_spec.is_valid())
    {
        // whatever is left in the m_incoming array, divided then by the number of channels
        samples = m_incoming.get_size() - m_incoming_ptr;
        latency_sec += audio_math::samples_to_time(samples / m_incoming_spec.m_channels, m_incoming_spec.m_sample_rate);
    }

    // get the latency for the currently active spec if the stream has not been drained
    if (m_active_spec.is_valid() && stream && !drained)
    {
        if (!(timing_info = g_pa_stream_get_timing_info(stream)))
        {
            // timing info received for the first time, log that for now: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a090147751441a97e04a4acef1d6514cb
            console::info("Received initial timing information");
        }

        // returns negative on error, 0 on success: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#aa521efcc16fe2abf0f8461462432ac16
        if (g_pa_stream_get_latency(stream, &latency_usec, NULL) == 0)
        {
            latency_sec += (latency_usec * 0.000001);
        }
        else
        {
            // need to update timing information
            console::info("Updating timing information");
            g_pa_threaded_mainloop_lock(mainloop);

            if (operation = g_pa_stream_update_timing_info(stream, stream_success_cb, mainloop))
            {
                while (g_pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
                {
                    g_pa_threaded_mainloop_wait(mainloop);
                }
                g_pa_operation_unref(operation);
            }

            g_pa_threaded_mainloop_unlock(mainloop);
            if (g_pa_stream_get_latency(stream, &latency_usec, NULL) == 0)
            {
                latency_sec += (latency_usec * 0.000001);
            }
            else
            {
                console::error("pa_stream_get_latency returned error after timing information update");
            }
        }
    }

    return latency_sec;
}

void output_pulse::process_samples(const audio_chunk &p_chunk)
{
    t_samplespec spec;

    // I dunno why we check for exactly this, maybe because we need to have processed all samples
    pfc::dynamic_assert(m_incoming_ptr == m_incoming.get_size());

    spec.fromchunk(p_chunk);
    if (spec.is_valid())
    {
        m_incoming.set_data_fromptr(p_chunk.get_data(), p_chunk.get_used_size());
        m_incoming_ptr = 0;
        m_incoming_spec = spec;
    }
    else
    {
        pfc::throw_exception_with_message<exception_io_data>("Invalid audio stream specifications");
    }
}

bool output_pulse::context_wait(pa_context* ctx, pa_threaded_mainloop* ml)
{
    pa_context_state_t state;
    while ((state = g_pa_context_get_state(ctx)) != PA_CONTEXT_READY)
    {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
        {
            return false;
        }
        g_pa_threaded_mainloop_wait(ml);
    }
    return 0;
}

void output_pulse::context_subscribe_cb(pa_context* c, pa_subscription_event_type_t t, uint32_t idx, void* userdata)
{
    if ((pa_subscription_event_type)(t & PA_SUBSCRIPTION_EVENT_SINK_INPUT) == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    {
        output_pulse* output = (output_pulse*)userdata;
        if (!(output->stream)) {
            return;
        }

        if (g_pa_stream_get_index(output->stream) == idx)
        {
            g_pa_context_get_sink_input_info(output->context, idx, sink_input_info_cb, output);
        }
    }
}

void output_pulse::sink_input_info_cb(pa_context* c, const pa_sink_input_info* i, int eol, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    if (!i || !output)
    {
        return;
    }

    if (g_pa_cvolume_valid(&i->volume) && output->volume != i->volume.values[0])
    {
        float volume_db = (float)g_pa_sw_volume_to_dB(i->volume.values[0]);
        fb2k::inMainThread([volume_db]()
        {
            playback_control::get()->set_volume(volume_db);
        });
    }
}

void output_pulse::context_state_cb(pa_context* ctx, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    std::stringstream s;
    switch (g_pa_context_get_state(ctx))
    {
    case PA_CONTEXT_FAILED:
        console_error("connection failed", g_pa_context_errno(ctx));
        stop();
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
        g_pa_threaded_mainloop_signal(output->mainloop, 0);
    }
}

int output_pulse::stream_wait(pa_stream* s, pa_threaded_mainloop* ml)
{
    pa_stream_state_t state;

    while ((state = g_pa_stream_get_state(s)) != PA_STREAM_READY)
    {
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
        {
            return -1;
        }
        g_pa_threaded_mainloop_wait(ml);
    }
    return 0;
}

void output_pulse::stream_state_cb(pa_stream* s, void* userdata)
{
    pa_threaded_mainloop* ml = (pa_threaded_mainloop*)userdata;

    switch (g_pa_stream_get_state(s))
    {
    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        g_pa_threaded_mainloop_signal(ml, 0);
    }
}

void output_pulse::stream_started_cb(pa_stream* s, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    output->progressing = true;
}

void output_pulse::stream_underflow_cb(pa_stream* s, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    output->progressing = false;
    output->trigger_update.set_state(true);
}

void output_pulse::stream_write_cb(pa_stream* s, size_t nbytes, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    output->trigger_update.set_state(true);
}

size_t output_pulse::write()
{
    if (!stream || m_incoming_spec != m_active_spec) {
      return 0;
    }

    g_pa_threaded_mainloop_lock(mainloop);

    if (next_write_relative) {
        const pa_timing_info* info = g_pa_stream_get_timing_info(stream);
        if (!info)
        {
            console::error("Error getting stream timing info");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        int64_t write_index = info->read_index - (info->read_index % (4 * m_active_spec.m_channels));

        const pa_buffer_attr* buffer_attr = g_pa_stream_get_buffer_attr(stream);
        if (!buffer_attr)
        {
            console::error("Error getting stream buffer attributes");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        size_t cw_samples = buffer_attr->tlength / sizeof(audio_sample);
        size_t delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, cw_samples);
        if (delta > 0)
        {
            int error = g_pa_stream_write(stream, m_incoming.get_ptr() + m_incoming_ptr,
            delta * sizeof(audio_sample), NULL, write_index, PA_SEEK_ABSOLUTE);
            if (error < 0)
            {
                console_error("error writing to stream", error);
                g_pa_threaded_mainloop_unlock(mainloop);
                return (cw_samples - delta) / m_incoming_spec.m_channels;
            }
            else
            {
                next_write_relative = false;
                m_incoming_ptr += delta;
            }
        }

        g_pa_threaded_mainloop_unlock(mainloop);
        return (cw_samples - delta) / m_incoming_spec.m_channels;
    }
    else
    {
        size_t cw_samples = g_pa_stream_writable_size(stream) / sizeof(audio_sample);
        if (cw_samples == (size_t)-1)
        {
            console_error("g_pa_stream_writable_size error", g_pa_context_errno(context));
            return 0;
        }

        size_t delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, cw_samples);

        if (delta > 0)
        {
            int error = g_pa_stream_write(stream, m_incoming.get_ptr() + m_incoming_ptr, delta * sizeof(audio_sample), NULL, 0, PA_SEEK_RELATIVE);
            if (error < 0)
            {
                console_error("error writing to stream", error);
                g_pa_threaded_mainloop_unlock(mainloop);
                return 0;
            }
            else
            {
                m_incoming_ptr += delta;
            }
        }

        g_pa_threaded_mainloop_unlock(mainloop);
        return (cw_samples - delta) / m_incoming_spec.m_channels;
    }
}

void output_pulse::stream_success_cb(pa_stream* s, int success, void* userdata)
{
    // signal all waiting threads: https://www.freedesktop.org/software/pulseaudio/doxygen/thread-mainloop_8h.html#ad253b70911af81c04417793841a15766
    g_pa_threaded_mainloop_signal((pa_threaded_mainloop*)userdata, 0);
}

void output_pulse::close_stream()
{
    if (stream)
    {
        g_pa_stream_set_state_callback(stream, NULL, NULL);
        g_pa_stream_set_started_callback(stream, NULL, NULL);
        g_pa_stream_set_underflow_callback(stream, NULL, NULL);
        g_pa_stream_set_write_callback(stream, NULL, NULL);
        g_pa_stream_disconnect(stream);
        g_pa_stream_unref(stream);
        stream = NULL;
        progressing = false;
    }
}

void output_pulse::open_incoming_spec()
{
    if (!m_incoming_spec.is_valid())
    {
        console::info("Invalid incoming_spec");
        return;
    }

    pa_sample_spec ss;
    ss.channels = m_incoming_spec.m_channels;
    ss.rate = m_incoming_spec.m_sample_rate;
    ss.format = PA_SAMPLE_FLOAT32LE;

    struct pa_channel_map map;
    const pa_channel_map* map_ptr = g_pa_channel_map_init_auto(&map, ss.channels, PA_CHANNEL_MAP_WAVEEX);

    pa_stream_flags_t flags = (pa_stream_flags_t)(PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE);

    struct pa_buffer_attr attr;
    attr.maxlength = (uint32_t)ceil(m_incoming_spec.time_to_samples(buffer_length + offset) * m_incoming_spec.m_channels * 4);
    attr.fragsize = 0;
    attr.minreq = cfg_pulseaudio_minreq_workaround.get() ? attr.maxlength / 2 : (uint32_t)-1;
    attr.tlength = attr.maxlength;
    attr.prebuf = (uint32_t)ceil(m_incoming_spec.time_to_samples(0.001 * cfg_pulseaudio_prebuf) * m_incoming_spec.m_channels * 4);

    std::stringstream s;
    s << "Pulseaudio: requesting buffer attributes: maxlength "
    << attr.maxlength << ", minreq " << attr.minreq << ", tlength "
    << attr.tlength << ", prebuf " << attr.prebuf;
    console::info(s.str().c_str());

    g_pa_threaded_mainloop_lock(mainloop);

    close_stream();

    stream = g_pa_stream_new(context, "Audio", &ss, map_ptr);
    progressing = false;
    if (!stream)
    {
        g_pa_threaded_mainloop_unlock(mainloop);
        console::error("Error creating stream");
        stop();
        return;
    }

    g_pa_stream_set_state_callback(stream, stream_state_cb, mainloop);
    g_pa_stream_set_started_callback(stream, stream_started_cb, this);
    g_pa_stream_set_underflow_callback(stream, stream_underflow_cb, this);
    g_pa_stream_set_write_callback(stream, stream_write_cb, this);

    int err = g_pa_stream_connect_playback(stream, NULL, &attr, flags, NULL, NULL);
    if (err < 0 || stream_wait(stream, mainloop))
    {
        g_pa_threaded_mainloop_unlock(mainloop);
        console_error("failed to connect stream", err);
        stop();
        return;
    }

    m_active_spec = m_incoming_spec;

    g_pa_threaded_mainloop_unlock(mainloop);
    trigger_update.set_state(true);
}

void output_pulse::console_error(const char* prefix, int error_code)
{
    std::stringstream s;
    s << "Pulseaudio: ";
    s << prefix;

    if (error_code != 0)
    {
        const char* error = g_pa_strerror(error_code);
        if (error)
        {
            s << ": " << error;
        }
    }

    console::error(s.str().c_str());
}

void output_pulse::stream_drained_cb(pa_stream* s, int success, void* userdata)
{
    output_pulse* output = (output_pulse*)userdata;
    output->draining = false;
    output->drained = true;
    output->trigger_update.set_state(true);
}

bool output_pulse::load_pulse_dll()
{
    pfc::string_formatter path;
    std::wstringstream wpath_libpulse;
    HMODULE libpulse;

    if (g_pa_is_loaded)
    {
        console::info("libpulse-0.dll already loaded");
        return true;
    }

    // construct path to libpulse-0.dll
    path = core_api::get_my_full_path();
    path.truncate(path.scan_filename());
    wpath_libpulse << path << "pulse\\libpulse-0.dll";
    libpulse = LoadLibraryExW(wpath_libpulse.str().c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (!libpulse) {
        // we don't really do much with the error code at this point
        console::error("Could not load libpulse-0.dll");
        return false;
    }

    // just assign and compare the value with null at the same if, since this is so horrible no matter how we do this
    if (!(g_pa_bytes_to_usec                    = (pa_bytes_to_usec)GetProcAddress(libpulse,                    "pa_bytes_to_usec")) ||
        !(g_pa_channel_map_init_auto            = (pa_channel_map_init_auto)GetProcAddress(libpulse,            "pa_channel_map_init_auto")) ||
        !(g_pa_context_connect                  = (pa_context_connect)GetProcAddress(libpulse,                  "pa_context_connect")) ||
        !(g_pa_context_disconnect               = (pa_context_disconnect)GetProcAddress(libpulse,               "pa_context_disconnect")) ||
        !(g_pa_context_errno                    = (pa_context_errno)GetProcAddress(libpulse,                    "pa_context_errno")) ||
        !(g_pa_context_get_sink_input_info      = (pa_context_get_sink_input_info)GetProcAddress(libpulse,      "pa_context_get_sink_input_info")) ||
        !(g_pa_context_get_state                = (pa_context_get_state)GetProcAddress(libpulse,                "pa_context_get_state")) ||
        !(g_pa_context_new_with_proplist        = (pa_context_new_with_proplist)GetProcAddress(libpulse,        "pa_context_new_with_proplist")) ||
        !(g_pa_context_set_event_callback       = (pa_context_set_event_callback)GetProcAddress(libpulse,       "pa_context_set_event_callback")) ||
        !(g_pa_context_set_sink_input_volume    = (pa_context_set_sink_input_volume)GetProcAddress(libpulse,    "pa_context_set_sink_input_volume")) ||
        !(g_pa_context_set_state_callback       = (pa_context_set_state_callback)GetProcAddress(libpulse,       "pa_context_set_state_callback")) ||
        !(g_pa_context_set_subscribe_callback   = (pa_context_set_subscribe_callback)GetProcAddress(libpulse,   "pa_context_set_subscribe_callback")) ||
        !(g_pa_context_subscribe                = (pa_context_subscribe)GetProcAddress(libpulse,                "pa_context_subscribe")) ||
        !(g_pa_context_unref                    = (pa_context_unref)GetProcAddress(libpulse,                    "pa_context_unref")) ||
        !(g_pa_cvolume_equal                    = (pa_cvolume_equal)GetProcAddress(libpulse,                    "pa_cvolume_equal")) ||
        !(g_pa_cvolume_init                     = (pa_cvolume_init)GetProcAddress(libpulse,                     "pa_cvolume_init")) ||
        !(g_pa_cvolume_set                      = (pa_cvolume_set)GetProcAddress(libpulse,                      "pa_cvolume_set")) ||
        !(g_pa_cvolume_valid                    = (pa_cvolume_valid)GetProcAddress(libpulse,                    "pa_cvolume_valid")) ||
        !(g_pa_operation_get_state              = (pa_operation_get_state)GetProcAddress(libpulse,              "pa_operation_get_state")) ||
        !(g_pa_operation_unref                  = (pa_operation_unref)GetProcAddress(libpulse,                  "pa_operation_unref")) ||
        !(g_pa_proplist_free                    = (pa_proplist_free)GetProcAddress(libpulse,                    "pa_proplist_free")) ||
        !(g_pa_proplist_new                     = (pa_proplist_new)GetProcAddress(libpulse,                     "pa_proplist_new")) ||
        !(g_pa_proplist_set                     = (pa_proplist_set)GetProcAddress(libpulse,                     "pa_proplist_set")) ||
        !(g_pa_proplist_setp                    = (pa_proplist_setp)GetProcAddress(libpulse,                    "pa_proplist_setp")) ||
        !(g_pa_proplist_sets                    = (pa_proplist_sets)GetProcAddress(libpulse,                    "pa_proplist_sets")) ||
        !(g_pa_stream_cancel_write              = (pa_stream_cancel_write)GetProcAddress(libpulse,              "pa_stream_cancel_write")) ||
        !(g_pa_stream_connect_playback          = (pa_stream_connect_playback)GetProcAddress(libpulse,          "pa_stream_connect_playback")) ||
        !(g_pa_stream_cork                      = (pa_stream_cork)GetProcAddress(libpulse,                      "pa_stream_cork")) ||
        !(g_pa_stream_disconnect                = (pa_stream_disconnect)GetProcAddress(libpulse,                "pa_stream_disconnect")) ||
        !(g_pa_stream_drain                     = (pa_stream_drain)GetProcAddress(libpulse,                     "pa_stream_drain")) ||
        !(g_pa_stream_drop                      = (pa_stream_drop)GetProcAddress(libpulse,                      "pa_stream_drop")) ||
        !(g_pa_stream_flush                     = (pa_stream_flush)GetProcAddress(libpulse,                     "pa_stream_flush")) ||
        !(g_pa_stream_get_buffer_attr           = (pa_stream_get_buffer_attr)GetProcAddress(libpulse,           "pa_stream_get_buffer_attr")) ||
        !(g_pa_stream_get_index                 = (pa_stream_get_index)GetProcAddress(libpulse,                 "pa_stream_get_index")) ||
        !(g_pa_stream_get_latency               = (pa_stream_get_latency)GetProcAddress(libpulse,               "pa_stream_get_latency")) ||
        !(g_pa_stream_get_sample_spec           = (pa_stream_get_sample_spec)GetProcAddress(libpulse,           "pa_stream_get_sample_spec")) ||
        !(g_pa_stream_get_state                 = (pa_stream_get_state)GetProcAddress(libpulse,                 "pa_stream_get_state")) ||
        !(g_pa_stream_get_timing_info           = (pa_stream_get_timing_info)GetProcAddress(libpulse,           "pa_stream_get_timing_info")) ||
        !(g_pa_stream_is_corked                 = (pa_stream_is_corked)GetProcAddress(libpulse,                 "pa_stream_is_corked")) ||
        !(g_pa_stream_new                       = (pa_stream_new)GetProcAddress(libpulse,                       "pa_stream_new")) ||
        !(g_pa_stream_set_started_callback      = (pa_stream_set_started_callback)GetProcAddress(libpulse,      "pa_stream_set_started_callback")) ||
        !(g_pa_stream_set_state_callback        = (pa_stream_set_state_callback)GetProcAddress(libpulse,        "pa_stream_set_state_callback")) ||
        !(g_pa_stream_set_underflow_callback    = (pa_stream_set_underflow_callback)GetProcAddress(libpulse,    "pa_stream_set_underflow_callback")) ||
        !(g_pa_stream_set_write_callback        = (pa_stream_set_write_callback)GetProcAddress(libpulse,        "pa_stream_set_write_callback")) ||
        !(g_pa_stream_trigger                   = (pa_stream_trigger)GetProcAddress(libpulse,                   "pa_stream_trigger")) ||
        !(g_pa_stream_unref                     = (pa_stream_unref)GetProcAddress(libpulse,                     "pa_stream_unref")) ||
        !(g_pa_stream_update_sample_rate        = (pa_stream_update_sample_rate)GetProcAddress(libpulse,        "pa_stream_update_sample_rate")) ||
        !(g_pa_stream_update_timing_info        = (pa_stream_update_timing_info)GetProcAddress(libpulse,        "pa_stream_update_timing_info")) ||
        !(g_pa_stream_writable_size             = (pa_stream_writable_size)GetProcAddress(libpulse,             "pa_stream_writable_size")) ||
        !(g_pa_stream_write                     = (pa_stream_write)GetProcAddress(libpulse,                     "pa_stream_write")) ||
        !(g_pa_strerror                         = (pa_strerror)GetProcAddress(libpulse,                         "pa_strerror")) ||
        !(g_pa_sw_volume_from_dB                = (pa_sw_volume_from_dB)GetProcAddress(libpulse,                "pa_sw_volume_from_dB")) ||
        !(g_pa_sw_volume_to_dB                  = (pa_sw_volume_to_dB)GetProcAddress(libpulse,                  "pa_sw_volume_to_dB")) ||
        !(g_pa_threaded_mainloop_accept         = (pa_threaded_mainloop_accept)GetProcAddress(libpulse,         "pa_threaded_mainloop_accept")) ||
        !(g_pa_threaded_mainloop_free           = (pa_threaded_mainloop_free)GetProcAddress(libpulse,           "pa_threaded_mainloop_free")) ||
        !(g_pa_threaded_mainloop_get_api        = (pa_threaded_mainloop_get_api)GetProcAddress(libpulse,        "pa_threaded_mainloop_get_api")) ||
        !(g_pa_threaded_mainloop_get_retval     = (pa_threaded_mainloop_get_retval)GetProcAddress(libpulse,     "pa_threaded_mainloop_get_retval")) ||
        !(g_pa_threaded_mainloop_lock           = (pa_threaded_mainloop_lock)GetProcAddress(libpulse,           "pa_threaded_mainloop_lock")) ||
        !(g_pa_threaded_mainloop_new            = (pa_threaded_mainloop_new)GetProcAddress(libpulse,            "pa_threaded_mainloop_new")) ||
        !(g_pa_threaded_mainloop_signal         = (pa_threaded_mainloop_signal)GetProcAddress(libpulse,         "pa_threaded_mainloop_signal")) ||
        !(g_pa_threaded_mainloop_start          = (pa_threaded_mainloop_start)GetProcAddress(libpulse,          "pa_threaded_mainloop_start")) ||
        !(g_pa_threaded_mainloop_stop           = (pa_threaded_mainloop_stop)GetProcAddress(libpulse,           "pa_threaded_mainloop_stop")) ||
        !(g_pa_threaded_mainloop_unlock         = (pa_threaded_mainloop_unlock)GetProcAddress(libpulse,         "pa_threaded_mainloop_unlock")) ||
        !(g_pa_threaded_mainloop_wait           = (pa_threaded_mainloop_wait)GetProcAddress(libpulse,           "pa_threaded_mainloop_wait")) ||
        !(g_pa_usec_to_bytes                    = (pa_usec_to_bytes)GetProcAddress(libpulse,                    "pa_usec_to_bytes")))
    {
        console::error("Error loading external functions from libpulse-0.dll");
        return false;
    }

    // set our flag to indicate success
    g_pa_is_loaded = true;
    console_message("Successfully loaded libpulse-0.dll");
    return true;
}

void output_pulse::g_enum_devices(output_device_enum_callback& p_callback)
{
    // dunno how the name change is handled, but as tcp4:127.0.0.1 is my only ever use case under wine I don't really care
    pfc::string8 pulseaudio_server_string;

    // run the callback if the pulseaudio libraries are or have been loaded successfully
    if (load_pulse_dll())
    {
        cfg_pulseaudio_server.get(pulseaudio_server_string);
        p_callback.on_device(guid_cfg_pulseaudio_device, pulseaudio_server_string, 9);
    }
}

void output_pulse::console_message(const char* format, ...)
{
    const size_t buffer_size = 2048;
    char buffer[buffer_size];
    va_list p_arg;

    va_start(p_arg, format);
    if (vsnprintf_s(buffer, buffer_size, format, p_arg) > -1)
    {
        // just dump to info
        console::info(buffer);
    }
    else
    {
        console::error("vsnprintf_s returned error");
    }
    va_end(p_arg);
}
#include "stdafx.h"
#include "output_pulseaudio.h"

output_pulse::output_pulse(const GUID& p_device, double p_buffer_length, bool p_dither, t_uint32 p_bitdepth)
      : buffer_length(p_buffer_length),
        next_write_relative(false),
        volume(0)
{
    stream = NULL;
    context = NULL;
    mainloop = NULL;
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
        console_error("pa_threaded_mainloop_start");
        stop();
        return;
    }

    pa_proplist* proplist = g_pa_proplist_new();
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "foobar2000");
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "foobar2000");
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "foobar2000");

    pa_mainloop_api* api;
    g_pa_threaded_mainloop_lock(mainloop);
    api = g_pa_threaded_mainloop_get_api(mainloop);
    context = g_pa_context_new_with_proplist(api, "foobar2000", proplist);
    if (proplist)
    {
        g_pa_proplist_free(proplist);
    }

    // notifies context_state_cb when the server connection is established below
    g_pa_context_set_state_callback(context, context_state_cb, mainloop);

    if (!context_connect())
    {
        g_pa_context_unref(context);
        context = NULL;
        g_pa_threaded_mainloop_unlock(mainloop);
        g_pa_threaded_mainloop_stop(mainloop);
        g_pa_threaded_mainloop_free(mainloop);
        mainloop = NULL;
        stop();
        return;
    }

    g_pa_threaded_mainloop_unlock(mainloop);
}

bool output_pulse::context_connect()
{
    pfc::string8 server_string;
    pa_context_state_t state;

    // read server connection string from settings
    cfg_pulseaudio_server.get(server_string);

    // connect context to server, returns negative on certain errors: https://www.freedesktop.org/software/pulseaudio/doxygen/context_8h.html#a983ce13d45c5f4b0db8e1a34e21f9fce
    if (g_pa_context_connect(context, server_string, (pa_context_flags_t)0, NULL) < 0)
    {
        console_error("pa_context_connect");
        return false;
    }

    // wait until ready
    while ((state = g_pa_context_get_state(context)) != PA_CONTEXT_READY)
    {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
        {
            console_error("pa_context_get_state");
            return false;
        }

        g_pa_threaded_mainloop_wait(mainloop);
    }

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

void output_pulse::flush()
{
    m_incoming_ptr = 0;
    m_incoming.set_size(0);
    next_write_relative = true;
}

size_t output_pulse::update_v2()
{
    m_can_write = 0;

    //on_update(); // TODO

    if (!m_incoming_spec.is_valid()) return SIZE_MAX;

    if (m_incoming_spec != m_active_spec)
    {
        if (drained || next_write_relative)
        {
            next_write_relative = false;
            drained = false;
            open(m_incoming_spec);
            m_active_spec = m_incoming_spec;
        }
        else
        {
            force_play(); // TODO
            return 0;
        }
    }

    if (m_incoming_ptr < m_incoming.get_size())
    {
        m_can_write = write();
    }
    else if (m_incoming_ptr == m_incoming.get_size())
    {
        m_can_write = SIZE_MAX;
    }
    return m_can_write;
}

void output_pulse::open(audio_chunk::spec_t const& p_spec)
{
    pa_sample_spec ss;
    pa_buffer_attr attr;

    ss.channels = p_spec.chanCount;
    ss.rate = p_spec.sampleRate;
    ss.format = PA_SAMPLE_FLOAT32LE;

    attr.maxlength = (uint32_t)ceil(audio_math::time_to_samples(buffer_length, p_spec.sampleRate) * p_spec.chanCount * sizeof(audio_sample));
    attr.tlength = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.fragsize = 0;

    g_pa_threaded_mainloop_lock(mainloop);
    
    if (stream)
    {
        g_pa_stream_set_state_callback(stream, NULL, NULL);
        g_pa_stream_disconnect(stream);
        g_pa_stream_unref(stream);
        stream = NULL;
    }
    
    if (!stream_connect(&ss, &attr))
    {
        g_pa_threaded_mainloop_unlock(mainloop);
        stop();
        return;
    }

    g_pa_threaded_mainloop_unlock(mainloop);
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

output_v8::latencyInfo_t output_pulse::get_latency_info()
{
    latencyInfo_t ret = {};
    pa_usec_t latency_usec;
    pa_operation* op;

    if (m_incoming_spec.is_valid()) {
        ret.latency += audio_math::samples_to_time((m_incoming.get_size() - m_incoming_ptr) / m_incoming_spec.chanCount, m_incoming_spec.sampleRate);
    }

    if (m_active_spec.is_valid() && stream && !drained) {
        
        g_pa_stream_get_timing_info(stream);
        if (g_pa_stream_get_latency(stream, &latency_usec, NULL) == 0)
        {
            ret.latency += (latency_usec * 0.000001);
            ret.hardQueued += ret.latency; // TODO
        }
        else
        {
            g_pa_threaded_mainloop_lock(mainloop);
            if (op = g_pa_stream_update_timing_info(stream, stream_success_cb, mainloop))
            {
                while (g_pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                {
                    g_pa_threaded_mainloop_wait(mainloop);
                }
                g_pa_operation_unref(op);
            }
            g_pa_threaded_mainloop_unlock(mainloop);

            if (g_pa_stream_get_latency(stream, &latency_usec, NULL) == 0)
            {
                ret.latency += (latency_usec * 0.000001);
                ret.hardQueued += ret.latency; // TODO
            }
            else
            {
                console_error("pa_stream_get_latency");
            }
        }
    }
    return ret;
}

size_t output_pulse::write()
{
    const pa_timing_info* timing_info;
    const pa_buffer_attr* buffer_attr;
    int64_t write_index;
    size_t cw_samples, delta;
    int err;
    // the number of bytes requested by the server that have not yet been written
    size_t requested_bytes;

    if (!stream || m_incoming_spec != m_active_spec) {
      return 0;
    }

    // lockity lock
    g_pa_threaded_mainloop_lock(mainloop);

    // if we are seeking...
    if (next_write_relative) {
        // need this to get the "current read index into the playback buffer in bytes": https://www.freedesktop.org/software/pulseaudio/doxygen/structpa__timing__info.html#a5e04baf968cc1d53a7795a58b2e4f788
        if (!(timing_info = g_pa_stream_get_timing_info(stream)))
        {
            console_error("pa_stream_get_timing_info");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        // "Return the per-stream server-side buffer metrics of the stream": https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a9a3c3e78eafb28cce3a16cef2b68a385 
        if (!(buffer_attr = g_pa_stream_get_buffer_attr(stream)))
        {
            console_error("pa_stream_get_buffer_attr");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        // calculate our "write index". I wonder what the magic number 4 is. at least it's sizeof(audio_sample)
        write_index = timing_info->read_index - (timing_info->read_index % (sizeof(audio_sample) * m_active_spec.chanCount));
        // sample count? is the "target length of the buffer" divided by the size of audio sample. makes sense
        cw_samples = buffer_attr->tlength / sizeof(audio_sample);
        // delta is the minimum of remaining buffer and audio samples
        delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, cw_samples);

        // thus, if we are not at the end and we have samples, we should write them
        if (delta > 0)
        {
            // right, so this differs with the other case by having seek mode set to absolute: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a4fc69dec0cc202fcc174125dc88dada7
            // so we write to our stream at the pointer position the size of our data, and absolute seek at the write index
            err = g_pa_stream_write(stream, m_incoming.get_ptr() + m_incoming_ptr, delta * sizeof(audio_sample), NULL, write_index, PA_SEEK_ABSOLUTE);
            if (err)
            {
                pa_console_error("pa_stream_write", err);
                g_pa_threaded_mainloop_unlock(mainloop);
                // and returns the remaining sample count without channel information
                return (cw_samples - delta) / m_incoming_spec.chanCount;
            }
            else
            {
                // on success we put our next write to non_relative and advance our pointer by how many samples we have written
                next_write_relative = false;
                m_incoming_ptr += delta;
            }
        }

        g_pa_threaded_mainloop_unlock(mainloop);

        // and we return the same stuff
        return (cw_samples - delta) / m_incoming_spec.chanCount;
    }
    else
    {
        // "Return the number of bytes requested by the server that have not yet been written.": https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a8927ec9a2876cf258cf1ffdb154b0362
        if ((requested_bytes = g_pa_stream_writable_size(stream)) == (size_t)-1)
        {
            console_error("pa_stream_writable_size");
            return 0;
        }

        // here our sample count is the number of bytes divided by sample size
        cw_samples = requested_bytes / sizeof(audio_sample);
        // and our delta is the same
        delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, cw_samples);

        if (delta > 0)
        {
            // fairly similar, except write at seek offset 0 with relative seek mode
            err = g_pa_stream_write(stream, m_incoming.get_ptr() + m_incoming_ptr, delta * sizeof(audio_sample), NULL, 0, PA_SEEK_RELATIVE);
            if (err < 0)
            {
                pa_console_error("g_pa_stream_write", err);
                g_pa_threaded_mainloop_unlock(mainloop);
                // returning 0 here means we are not yet ready to receive any data
                return 0;
            }
            else
            {
                m_incoming_ptr += delta;
            }
        }

        g_pa_threaded_mainloop_unlock(mainloop);
        return (cw_samples - delta) / m_incoming_spec.chanCount;
    }
}

bool output_pulse::stream_connect(const pa_sample_spec* ss, const pa_buffer_attr* attr)
{
    pa_stream_flags_t flags;
    pa_channel_map map;
    pa_channel_map* p_map;
    pa_stream_state_t state;

    // smooth graphs and automatic timing updates, whereever that matters
    flags = (pa_stream_flags_t)(PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE);

    // returns null if no mapping is found, and that is also valid: https://freedesktop.org/software/pulseaudio/doxygen/channelmap_8h.html#ab7d13111387d169484853f713b68f9cc
    // waveex uses microsoft's waveformatextensible mapping, which is how it originally was in the code: https://freedesktop.org/software/pulseaudio/doxygen/channelmap_8h.html#a61d273ea6bd3f09d79ffdec9e084f137
    p_map = g_pa_channel_map_init_auto(&map, ss->channels, PA_CHANNEL_MAP_WAVEEX);

    if (!(stream = g_pa_stream_new(context, "Audio", ss, p_map)))
    {
        console_error("pa_stream_new");
        return false;
    }

    // set callbacks
    g_pa_stream_set_state_callback(stream, stream_state_cb, mainloop);

    // returns zero on success: https://freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#ab9544f6677af133fbe81bf8a21eb489c
    if (g_pa_stream_connect_playback(stream, NULL, attr, flags, NULL, NULL) != 0)
    {
        console_error("pa_stream_connect_playback");
        return false;
    }

    while ((state = g_pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
        {
            console_error("pa_stream_get_state");
            return false;
        }
        g_pa_threaded_mainloop_wait(mainloop);
    }

    return true;
}

void output_pulse::pa_console_error(const char *name, int err)
{
    const char* s_err;

    if (s_err = g_pa_strerror(err))
    {
        console_error("%s: %s", name, s_err);
    }
    else
    {
        console_error("%s: unknown error", name);
    }
}

void output_pulse::console_message(Severity severity, const char* format, va_list args)
{
    const size_t buffer_size = 2048;
    char buffer[buffer_size];

    if (vsnprintf_s(buffer, buffer_size, format, args) < 0)
    {
        console::error("vsnprintf_s: unknown error");
    }
    else
    {
        switch (severity)
        {
        case Error:
            console::error(buffer);
            break;
        default:
            // this catches Info so no need to handle that separately
            console::info(buffer);
            break;
        }
    }
}

void output_pulse::console_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    console_message(Error, format, args);
    va_end(args);
}

void output_pulse::console_info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    console_message(Info, format, args);
    va_end(args);
}

bool output_pulse::load_pulse_dll()
{
    pfc::string_formatter component_path;
    std::wstringstream libpulse_dll_path;

    component_path = core_api::get_my_full_path();
    component_path.truncate(component_path.scan_filename());
    libpulse_dll_path << component_path << "PulseAudio-" << PLATFORM << std::filesystem::path::preferred_separator << "libpulse-0.dll";

    if (!g_pa_load(libpulse_dll_path.str()))
    {
        console_error("Could not load libpulse-0.dll");
        return false;
    }
    
    return true;
}

void output_pulse::g_enum_devices(output_device_enum_callback& p_callback)
{
    pfc::string server;
    cfg_pulseaudio_server.get(server);

    if (load_pulse_dll())
    {
        p_callback.on_device(guid_cfg_pulseaudio_device, server, server.length());
    }
}

// okay, this was literally just output_impl::process_samples(const audio_chunk & p_chunk) in the SDK with fade in/out additions
// TODO: I wonder if we need to even defined this as it's identical, but no time to check now
void output_pulse::process_samples(const audio_chunk& p_chunk) {
	PFC_ASSERT(queue_empty());
	PFC_ASSERT(!m_eos);
    const auto spec = p_chunk.get_spec();
    if (!spec.is_valid()) pfc::throw_exception_with_message< exception_io_data >("Invalid audio stream specifications");
    m_incoming_spec = spec;
    t_size length = p_chunk.get_used_size();
    m_incoming.set_data_fromptr(p_chunk.get_data(), length);
    m_incoming_ptr = 0;
}

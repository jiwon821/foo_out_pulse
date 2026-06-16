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
        console_error("pa_threaded_mainloop_start");
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

    // subscribe to event notifications: https://www.freedesktop.org/software/pulseaudio/doxygen/subscribe_8h.html#abe684246fd5cb640b0199bcfe7f801b0
    if (operation = g_pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL))
    {
        g_pa_operation_unref(operation);
    }

    // call context_subscribe_callback on events: https://www.freedesktop.org/software/pulseaudio/doxygen/subscribe_8h.html#a55281f798863e7b37594d347be7ad98c
    g_pa_context_set_subscribe_callback(context, context_subscribe_cb, this);

    console_info("pa_context_connect success");
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

    // apparently at least next_write_relative is needed here
    // well, of course it is, since it marks if we are seeking or not and flush() is called after seeking
    next_write_relative = true;
    trigger_update.set_state(true);
}

// it just calls flush() in output.h
//void output_pulse::flush_changing_track()

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
        // whatever is left in the m_i ncoming array, divided then by the number of channels
        samples = m_incoming.get_size() - m_incoming_ptr;
        latency_sec += audio_math::samples_to_time(samples / m_incoming_spec.m_channels, m_incoming_spec.m_sample_rate);
    }

    // get the latency for the currently active spec if the stream has not been drained
    if (m_active_spec.is_valid() && stream && !drained)
    {
        if (!(timing_info = g_pa_stream_get_timing_info(stream)))
        {
            // timing info received for the first time, log that for now: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#a090147751441a97e04a4acef1d6514cb
            console_info("Received initial timing information");
        }

        // returns negative on error, 0 on success: https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#aa521efcc16fe2abf0f8461462432ac16
        if (g_pa_stream_get_latency(stream, &latency_usec, NULL) == 0)
        {
            latency_sec += (latency_usec * 0.000001);
        }
        else
        {
            // need to update timing information
            console_info("Updating timing information");
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
                console_error("pa_stream_get_latency returned error after timing information update");
            }
        }
    }

    return latency_sec;
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
    output_pulse* o = (output_pulse*)userdata;
    if (!i || !o)
    {
        return;
    }

    if (g_pa_cvolume_valid(&i->volume) && o->volume != i->volume.values[0])
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
        console_error("pa_context_get_state", g_pa_context_errno(ctx));
        stop();
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
        g_pa_threaded_mainloop_signal(output->mainloop, 0);
    }
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

void output_pulse::stream_underflow_cb(pa_stream* s, void* userdata)
{
    output_pulse* o = (output_pulse*)userdata;
    o->progressing = false;
    o->trigger_update.set_state(true);
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
        // see also open_incoming_spec() for the magic number 4
        write_index = timing_info->read_index - (timing_info->read_index % (sizeof(audio_sample) * m_active_spec.m_channels));
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
                return (cw_samples - delta) / m_incoming_spec.m_channels;
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
        return (cw_samples - delta) / m_incoming_spec.m_channels;
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
        return (cw_samples - delta) / m_incoming_spec.m_channels;
    }
}


void output_pulse::stream_drained_cb(pa_stream* s, int success, void* userdata)
{
    output_pulse* o = (output_pulse*)userdata;
    o->draining = false;
    o->drained = true;
    o->trigger_update.set_state(true);
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
    g_pa_stream_set_started_callback(stream, stream_started_cb, this);
    g_pa_stream_set_underflow_callback(stream, stream_underflow_cb, this);
    g_pa_stream_set_write_callback(stream, stream_write_cb, this);

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

void output_pulse::open_incoming_spec()
{
    pa_sample_spec ss;
    pa_buffer_attr attr;

    if (!m_incoming_spec.is_valid())
    {
        console_info("Invalid incoming_spec");
        return;
    }

    // always uses the 32-bit float format, probably doesn't make a difference
    ss.channels = m_incoming_spec.m_channels;
    ss.rate = m_incoming_spec.m_sample_rate;
    ss.format = PA_SAMPLE_FLOAT32LE;

    // maximum length of the buffer in bytes
    // TODO: ceil needed? why times four? offset is 0.05, why? audio_sample is typedef to float, so uhh, replace the * 4 with that
    //attr.maxlength = (uint32_t)ceil(m_incoming_spec.time_to_samples(buffer_length + offset) * m_incoming_spec.m_channels * sizeof(audio_sample));
    attr.maxlength = (uint32_t)ceil(m_incoming_spec.time_to_samples(buffer_length) * m_incoming_spec.m_channels * sizeof(audio_sample));
    // playback only: "recommended to set this to (uint32_t) -1, which will initialize this to a value that is deemed sensible by the server
    // dunno why attr.maxlength was used before
    attr.tlength = (uint32_t)-1;
    // "server does not request less than minreq bytes from the client", "recommended to set this to (uint32_t) -1"
    // here was the minreq workaround, so maybe will have to return to this later
    //attr.minreq = cfg_pulseaudio_minreq_workaround.get() ? attr.maxlength / 2 : (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    // "server does not start with playback before at least prebuf bytes are available in the buffer", "recommended to set this to (uint32_t) -1, which will initialize this to the same value as tlength"
    // TODO: the original is weird
    //attr.prebuf = (uint32_t)ceil(m_incoming_spec.time_to_samples(0.001 * cfg_pulseaudio_prebuf) * m_incoming_spec.m_channels * 4);
    attr.prebuf = (uint32_t)-1;
    // recording only: fragment size, just zero it out
    attr.fragsize = 0;

    console_info("requesting buffer attributes: maxlength %zu, minreq %zu, tlength %zu, prebuf %zu", attr.maxlength, attr.minreq, attr.tlength, attr.prebuf);

    g_pa_threaded_mainloop_lock(mainloop);

    // I guess we close before creating a new stream
    close_stream();
    // hmm
    progressing = false;

    if (!stream_connect(&ss, &attr))
    {
        g_pa_threaded_mainloop_unlock(mainloop);
        stop();
        return;
    }

    m_active_spec = m_incoming_spec;
    g_pa_threaded_mainloop_unlock(mainloop);
    trigger_update.set_state(true);
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
    pfc::string_formatter path;
    std::wstringstream wpath_libpulse;

    // libpulse also checks this, maybe we could speed things up here, but leave out for now
    //if (g_pa_is_loaded)
    //{
    //    return true;
    //}

    // load libpulse-0.dll from the pulse/ directory relative to this component
    path = core_api::get_my_full_path();
    path.truncate(path.scan_filename());
    wpath_libpulse << path << "pulse\\libpulse-0.dll";

    if (!g_pa_load(wpath_libpulse.str()))
    {
        // we don't really do much with the error
        console_error("Could not load libpulse-0.dll");
        return false;
    }
    
    return true;
}

void output_pulse::g_enum_devices(output_device_enum_callback& p_callback)
{
    // dunno how the name change is handled, but as tcp4:127.0.0.1 is my only ever use case under wine I don't really care
    // get the server string from advanced settings
    pfc::string8 pulseaudio_server_string;
    cfg_pulseaudio_server.get(pulseaudio_server_string);

    // run the callback if the pulseaudio libraries are or have been loaded successfully
    if (load_pulse_dll())
    {
        p_callback.on_device(guid_cfg_pulseaudio_device, pulseaudio_server_string, 9);
    }
}

// okay, this was literally just output_impl::process_samples(const audio_chunk & p_chunk) in the SDK with fade in/out additions
// TODO: I wonder if we need to even defined this as it's identical, but no time to check now
void output_pulse::process_samples(const audio_chunk& p_chunk) {
    pfc::dynamic_assert(m_incoming_ptr == m_incoming.get_size());
    t_samplespec spec;
    spec.fromchunk(p_chunk);
    if (!spec.is_valid()) pfc::throw_exception_with_message< exception_io_data >("Invalid audio stream specifications");
    m_incoming_spec = spec;
    t_size length = p_chunk.get_used_size();
    m_incoming.set_data_fromptr(p_chunk.get_data(), length);
    m_incoming_ptr = 0;
}

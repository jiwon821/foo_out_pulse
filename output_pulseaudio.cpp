#include "stdafx.h"
#include "output_pulseaudio.h"

output_pulse::output_pulse(const GUID& p_device, double p_buffer_length, bool p_dither, t_uint32 p_bitdepth)
      : buffer_length(p_buffer_length),
        next_write_relative(false),
        stream(NULL),
        context(NULL),
        mainloop(NULL),
        draining(false),
        drained(false),
        m_incoming_ptr(0)
{
    pa_context_state_t state;
    pa_mainloop_api* api;
    pa_proplist* proplist;
    pfc::string server;

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
        console::error("pa_threaded_mainloop_start");
        stop();
        return;
    }

    proplist = g_pa_proplist_new();
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, APPLICATION_NAME);
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, APPLICATION_ID);
    g_pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, APPLICATION_ICON_NAME);

    g_pa_threaded_mainloop_lock(mainloop);
    api = g_pa_threaded_mainloop_get_api(mainloop);
    context = g_pa_context_new_with_proplist(api, "foobar2000", proplist);
    g_pa_proplist_free(proplist);

    g_pa_context_set_state_callback(context, context_state_cb, mainloop);

    cfg_pulseaudio_server.get(server);
    if (g_pa_context_connect(context, server, (pa_context_flags_t)0, NULL) < 0)
    {
        g_pa_context_unref(context);
        context = NULL;
        g_pa_threaded_mainloop_unlock(mainloop);
        g_pa_threaded_mainloop_stop(mainloop);
        g_pa_threaded_mainloop_free(mainloop);
        mainloop = NULL;
        console::error("pa_context_connect");
        stop();
        return;
    }

    while ((state = g_pa_context_get_state(context)) != PA_CONTEXT_READY)
    {
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
        {
            g_pa_context_unref(context);
            context = NULL;
            g_pa_threaded_mainloop_unlock(mainloop);
            g_pa_threaded_mainloop_stop(mainloop);
            g_pa_threaded_mainloop_free(mainloop);
            mainloop = NULL;
            console::error("pa_context_get_state");
            stop();
            return;
        }

        g_pa_threaded_mainloop_wait(mainloop);
    }

    g_pa_threaded_mainloop_unlock(mainloop);
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
        g_pa_threaded_mainloop_free(mainloop);
    }
}

void output_pulse::pause(bool p_state)
{
    pa_operation *op;
    if (stream)
    {
        g_pa_threaded_mainloop_lock(mainloop);
        if (op = g_pa_stream_cork(stream, p_state, NULL, NULL)) {
            g_pa_operation_unref(op);
        }
        g_pa_threaded_mainloop_unlock(mainloop);
    }
}

void output_pulse::force_play() {
    if (m_eos) return;
    m_eos = true;
    if (queue_empty()) send_force_play();
}
void output_pulse::send_force_play() {
    if (m_sent_force_play) return;
    m_sent_force_play = true;
    this->on_force_play();
}

void output_pulse::flush()
{
    m_incoming_ptr = 0;
    m_incoming.set_size(0);
    next_write_relative = true;
}

size_t output_pulse::update_v2()
{
    // Clear preemptively
    m_can_write = 0;

    on_update();

    // No data yet, nothing to do, want data, can't signal how much because we don't know the format
    if (!m_incoming_spec.is_valid()) return SIZE_MAX;

    // First chunk in or format change
    if (m_incoming_spec != m_active_spec) {
        if (drained || next_write_relative)
        {
            next_write_relative = false;
            drained = false;
            open(m_incoming_spec);
            m_active_spec = m_incoming_spec;
        }
        else
        {
            // Previous format still playing, accept no more data
            this->send_force_play();
            return 0;
        }
    }

    m_can_write = this->can_write_samples();
    
    if (m_incoming_ptr < m_incoming.get_size())
    {
        t_size delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, m_can_write * m_incoming_spec.chanCount);
        if (delta > 0)
        {
            PFC_ASSERT(!m_sent_force_play);
            write(audio_chunk_temp_impl(m_incoming.get_ptr() + m_incoming_ptr, delta / m_incoming_spec.chanCount, m_incoming_spec.sampleRate, m_incoming_spec.chanCount, m_incoming_spec.chanMask));
            m_incoming_ptr += delta;
        }
        
       m_can_write -= delta / m_incoming_spec.chanCount;
    }
    
    return m_can_write;
}

void output_pulse::write(const audio_chunk& p_data)
{
    const pa_timing_info* timing_info;
    int64_t offset;

    g_pa_threaded_mainloop_lock(mainloop);

    if (next_write_relative)
    {
        if (!(timing_info = g_pa_stream_get_timing_info(stream)))
        {
            console::error("pa_stream_get_timing_info");
            g_pa_threaded_mainloop_unlock(mainloop);
            return;
        }

        offset = timing_info->read_index - (timing_info->read_index % (sizeof(audio_sample) * m_active_spec.chanCount));

        if (g_pa_stream_write(stream, p_data.get_data(), p_data.get_data_size() * sizeof(audio_sample), NULL, offset, PA_SEEK_ABSOLUTE) < 0)
        {
            console::error("pa_stream_write");
        }
        else
        {
            next_write_relative = false;
        }
    }
    else
    {
        if (g_pa_stream_write(stream, p_data.get_data(), p_data.get_data_size()  * sizeof(audio_sample), NULL, 0, PA_SEEK_RELATIVE) < 0)
        {
            console::error("pa_stream_write");
        }
    }

    g_pa_threaded_mainloop_unlock(mainloop);
}

size_t output_pulse::can_write_samples()
{
    const pa_buffer_attr* buffer_attr;
    size_t ret, requested_bytes;
    
    g_pa_threaded_mainloop_lock(mainloop);

    if (next_write_relative)
    {
        if (!(buffer_attr = g_pa_stream_get_buffer_attr(stream)))
        {
            console::error("pa_stream_get_buffer_attr");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        ret = buffer_attr->tlength;
    }
    else
    {
        if ((requested_bytes = g_pa_stream_writable_size(stream)) == (size_t)-1)
        {
            console::error("pa_stream_writable_size");
            g_pa_threaded_mainloop_unlock(mainloop);
            return 0;
        }

        ret = requested_bytes;
    }

    g_pa_threaded_mainloop_unlock(mainloop);
    return ret / m_incoming_spec.chanCount / sizeof(audio_sample); // TODO
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

void output_pulse::on_force_play()
{
    pa_operation *operation;

    if (draining)
    {
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
                console::error("pa_stream_get_latency");
            }
        }
    }
    return ret;
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
        console::error("pa_stream_new");
        return false;
    }

    // set callbacks
    g_pa_stream_set_state_callback(stream, stream_state_cb, mainloop);

    // returns zero on success: https://freedesktop.org/software/pulseaudio/doxygen/stream_8h.html#ab9544f6677af133fbe81bf8a21eb489c
    if (g_pa_stream_connect_playback(stream, NULL, attr, flags, NULL, NULL) != 0)
    {
        console::error("pa_stream_connect_playback");
        return false;
    }

    while ((state = g_pa_stream_get_state(stream)) != PA_STREAM_READY)
    {
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
        {
            console::error("pa_stream_get_state");
            return false;
        }
        g_pa_threaded_mainloop_wait(mainloop);
    }

    return true;
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
        console::error("Could not load libpulse-0.dll");
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

static void spec_sanity(audio_chunk::spec_t const& spec) {
    if (!spec.is_valid()) pfc::throw_exception_with_message< exception_io_data >("Invalid audio stream specifications");
}

size_t output_pulse::process_samples_v2(const audio_chunk& p_chunk) {
    PFC_ASSERT(queue_empty());
    PFC_ASSERT(!m_eos);
    const auto spec = p_chunk.get_spec();
    if (m_incoming_spec != spec) {
        spec_sanity(spec);
        m_incoming_spec = spec;
        return 0;
    }

    auto in = p_chunk.get_sample_count();
    if (in > m_can_write) in = m_can_write;
    if (in > 0) {
        write(audio_chunk_partial_ref(p_chunk, 0, in));
        m_can_write -= in;
    }
    return in;
}

void output_pulse::process_samples(const audio_chunk& p_chunk) {
    PFC_ASSERT(queue_empty());
    PFC_ASSERT(!m_eos);
    const auto spec = p_chunk.get_spec();
    size_t taken = 0;
    if (m_incoming_spec == spec) {
        // Try bypassing intermediate buffer
        taken = this->process_samples_v2(p_chunk);
        if (taken == p_chunk.get_sample_count()) return; // all written, success
        taken *= spec.chanCount;
    }
    else {
        spec_sanity(spec);
        m_incoming_spec = spec;
    }
    // Queue what's left for update() to eat later
    m_incoming.set_data_fromptr(p_chunk.get_data() + taken, p_chunk.get_used_size() - taken);
    m_incoming_ptr = 0;
}
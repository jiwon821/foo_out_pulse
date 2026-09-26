#include "stdafx.h"
#include "output_pulseaudio.h"

// As I cannot for the love of me get the output_impl stuff to work correctly,
// this file contains all output_impl methods copy-pasted from the SDK (and
// converted to output_pulse methods). Hopefully will get rid of this file in
// the future.

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

void output_pulse::on_flush_internal() {
	m_eos = false; m_sent_force_play = false;
	m_incoming_ptr = 0;
	m_incoming.set_size(0);
}

void output_pulse::flush() {
	on_flush_internal();
	on_flush();
}

void output_pulse::update(bool& p_ready) {
	p_ready = update_v2() > 0;
}

size_t output_pulse::update_v2() {

	// Clear preemptively
	m_can_write = 0;

	on_update();

	// No data yet, nothing to do, want data, can't signal how much because we don't know the format
	if (!m_incoming_spec.is_valid()) return SIZE_MAX;

	// First chunk in or format change
	if (m_incoming_spec != m_active_spec) {
		if (get_latency_samples() == 0) {
			// Ready for new format
			m_sent_force_play = false;
			open(m_incoming_spec);
			m_active_spec = m_incoming_spec;
		}
		else {
			// Previous format still playing, accept no more data
			this->send_force_play();
			return 0;
		}
	}

	// opened for m_incoming_spec stream

	// Store & update m_can_write on our end
	// We don't know what can_write_samples() actually does, could be expensive, avoid calling it repeatedly
	m_can_write = this->can_write_samples();

	if (m_incoming_ptr < m_incoming.get_size()) {
		t_size delta = pfc::min_t(m_incoming.get_size() - m_incoming_ptr, m_can_write * m_incoming_spec.chanCount);
		if (delta > 0) {
			PFC_ASSERT(!m_sent_force_play);
			write(audio_chunk_temp_impl(m_incoming.get_ptr() + m_incoming_ptr, delta / m_incoming_spec.chanCount, m_incoming_spec.sampleRate, m_incoming_spec.chanCount, m_incoming_spec.chanMask));
			m_incoming_ptr += delta;
			if (m_eos && this->queue_empty()) {
				this->send_force_play();
			}
		}

		m_can_write -= delta / m_incoming_spec.chanCount;
	}
	return m_can_write;
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
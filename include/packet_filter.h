/*!
 * @file packet_filter.h
 *
 * @copyright
 * Copyright &copy; 2026, Christian Riede
 *
 * @copyright
 * All rights reserved.
 *
 * @copyright
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * @copyright
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * @copyright
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @copyright
 * EchoLink&reg; is a registered trademark of Synergenics, LLC
 *
 * @brief Filters for packets received from remote EchoLink hosts
 *
 * The proxy relays UDP packets from arbitrary remote hosts to its client.
 * These filters detect packets that would trigger known vulnerabilities in
 * EchoLink clients (in particular SvxLink's echolib before 26.05.2 /
 * echolib 1.3.8), so the proxy can drop them before they are forwarded.
 * This is a compensating control ("virtual patch") for clients that cannot
 * be updated right away; it does not replace updating the client.
 */

#ifndef PACKET_FILTER_H_
#define PACKET_FILTER_H_

#include <stddef.h>
#include <stdint.h>

/*!
 * @brief Maximum callsign length accepted in an RTCP SDES packet
 *
 * Matches the limit SvxLink's fix applies (sanitizeCallsign()).
 */
#define PACKET_FILTER_MAX_CALLSIGN_LEN 32

/*!
 * @brief Determine if a UDP data packet is a malformed NDATA packet
 *
 * Mitigation for CVE-2026-73152 (GHSA-2m63-p9h9-p5rf): SvxLink's
 * Qso::handleNonAudioPacket() parses NDATA (info/chat) packets received on
 * the EchoLink data port (5198) without validating their length or the
 * position of the NUL terminator.
 *
 * @param[in] buf Packet payload received from the remote host
 * @param[in] len Number of valid bytes in @p buf
 *
 * @returns 1 if the packet is a malformed NDATA packet, 0 otherwise
 */
int packet_filter_ndata_malformed(const uint8_t *buf, size_t len);

/*!
 * @brief Determine if a UDP control packet carries an unsafe SDES callsign
 *
 * Mitigation for GHSA-6w9x-486p-ggrj (finding 1): SvxLink takes the remote
 * callsign from the NAME item of RTCP SDES packets received on the EchoLink
 * control port (5199) without sanitizing it, and ModuleEchoLink inserts it
 * unescaped into TCL event strings (remote TCL command execution) and fixed
 * size buffers (e.g. APRS status reporting).
 *
 * The NAME item is located the same way SvxLink's parseSDES() does, and the
 * callsign is the first whitespace-separated token of it, as extracted by
 * both Dispatcher::ctrlDataReceived() and Qso::handleSdesPacket(). The
 * packet is considered unsafe if that token is longer than
 * ::PACKET_FILTER_MAX_CALLSIGN_LEN or contains any character other than
 * A-Z, a-z, 0-9, '-', '/', '*' and '_' (the characters SvxLink's fix keeps).
 *
 * @param[in] buf Packet payload received from the remote host
 * @param[in] len Number of valid bytes in @p buf
 *
 * @returns 1 if the packet carries an unsafe callsign, 0 otherwise
 */
int packet_filter_sdes_unsafe(const uint8_t *buf, size_t len);

/*!
 * @brief Determine if a UDP data packet is not a well-formed EchoLink packet
 *
 * Accepted and therefore forwarded are audio packets (first byte 0xc0) with
 * a complete 12-byte RTP header, at most 1024 payload bytes, and - unless
 * the payload type (second byte) is 0x96 - at least four GSM frames, as well
 * as NDATA packets that packet_filter_ndata_malformed() does not reject.
 * Everything else is rejected.
 *
 * @param[in] buf Packet payload received from the remote host
 * @param[in] len Number of valid bytes in @p buf
 *
 * @returns 1 if the packet should not be forwarded, 0 otherwise
 */
int packet_filter_data_invalid(const uint8_t *buf, size_t len);

/*!
 * @brief Determine if a UDP control packet is not a well-formed RTCP packet
 *
 * Accepted and therefore forwarded are RTCP packets of version 3 or 1,
 * without padding in the first subpacket, beginning with a sender or
 * receiver report and containing a source description or goodbye subpacket.
 * Everything else is rejected.
 *
 * @param[in] buf Packet payload received from the remote host
 * @param[in] len Number of valid bytes in @p buf
 *
 * @returns 1 if the packet should not be forwarded, 0 otherwise
 */
int packet_filter_control_invalid(const uint8_t *buf, size_t len);

/*! Size of the line buffer used by the directory stream filter */
#define PACKET_FILTER_DIR_LINE_LEN 256

/*!
 * @brief State of the directory listing filter for one TCP connection
 *
 * One instance belongs to a single TCP forwarding thread; it is not shared
 * between threads and needs no locking.
 */
struct packet_filter_dir {
	/*! Current parser state */
	int state;

	/*! Number of start marker bytes matched so far */
	size_t start_len;

	/*! Number of listing entries still expected */
	long remaining;

	/*! Buffer for the callsign or count line currently being read */
	uint8_t line[PACKET_FILTER_DIR_LINE_LEN];

	/*! Number of bytes in packet_filter_dir::line */
	size_t line_len;

	/*! Set when the callsign of the current entry is the "." marker */
	int call_dot;
};

/*!
 * @brief Initialize a directory listing filter
 *
 * @param[out] f Filter state to initialize
 */
void packet_filter_dir_init(struct packet_filter_dir *f);

/*!
 * @brief Pass a chunk of a directory server stream through the filter
 *
 * Reproduces the line structure EchoLink::Directory::handleCallList() reads
 * (start marker, entry count, then four lines per entry) and rewrites the
 * callsign line of every entry to contain only the characters valid in an
 * EchoLink callsign (A-Z, a-z, 0-9, '-', '/', '*', '_'), truncated to
 * ::PACKET_FILTER_MAX_CALLSIGN_LEN. The "." and " " marker lines and every
 * other line are forwarded unchanged. Any input that does not match the
 * expected structure switches the filter to passing the remaining bytes of
 * the connection through verbatim.
 *
 * The callsign rewrite never lengthens a line, but a line begun in an
 * earlier call may be emitted here, so @p out must have room for at least
 * @p in_len + ::PACKET_FILTER_DIR_LINE_LEN bytes.
 *
 * @param[in,out] f Filter state for this connection
 * @param[in] in Received bytes
 * @param[in] in_len Number of bytes in @p in
 * @param[out] out Buffer receiving the bytes to forward
 *
 * @returns Number of bytes written to @p out
 */
size_t packet_filter_dir_process(struct packet_filter_dir *f,
				 const uint8_t *in, size_t in_len,
				 uint8_t *out);

/*!
 * @brief Emit any bytes the filter is still holding back
 *
 * Called when the connection is closing so a partially received final line
 * is not lost.
 *
 * @param[in,out] f Filter state for this connection
 * @param[out] out Buffer receiving the bytes to forward (room for
 *                 ::PACKET_FILTER_DIR_LINE_LEN bytes)
 *
 * @returns Number of bytes written to @p out
 */
size_t packet_filter_dir_flush(struct packet_filter_dir *f, uint8_t *out);

#endif /* PACKET_FILTER_H_ */

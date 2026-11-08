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

#endif /* PACKET_FILTER_H_ */

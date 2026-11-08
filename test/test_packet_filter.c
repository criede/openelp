/*!
 * @file test_packet_filter.c
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
 * @brief Tests related to the packet filters
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "packet_filter.h"

/*! Size of the packet buffers used by the tests */
#define TEST_PACKET_LEN 600

/*!
 * @brief Build an EchoLink-style compound RTCP packet (RR + SDES)
 *
 * The SDES chunk holds a CNAME item and a NAME item with the given body.
 *
 * @param[out] out Buffer of at least ::TEST_PACKET_LEN bytes
 * @param[in] version RTP version to put into both headers (3 or 1)
 * @param[in] name NAME item body
 * @param[in] name_len Length of @p name in bytes (at most 255)
 *
 * @returns Length of the packet in bytes
 */
static size_t build_rtcp(uint8_t *out, uint8_t version, const char *name,
			 size_t name_len);

/*!
 * @brief Check one filter result and report a mismatch
 *
 * @param[in] what Description of the test case
 * @param[in] got Result returned by the filter
 * @param[in] expected Expected result
 *
 * @returns 0 on match, -EINVAL otherwise
 */
static int expect(const char *what, int got, int expected);

/*!
 * @brief Tests of the NDATA filter
 *
 * @returns 0 on success, negative ERRNO value on failure
 *
 * @test Tests of the NDATA filter
 */
static int test_ndata(void);

/*!
 * @brief Tests of the SDES callsign filter
 *
 * @returns 0 on success, negative ERRNO value on failure
 *
 * @test Tests of the SDES callsign filter
 */
static int test_sdes(void);

/*!
 * @brief Main entry point for packet filter tests
 *
 * @returns 0 on success, non-zero value on failure
 */
int main(void);

int main(void)
{
	int ret = 0;

	ret |= test_ndata();
	ret |= test_sdes();

	return ret;
}

static size_t build_rtcp(uint8_t *out, uint8_t version, const char *name,
			 size_t name_len)
{
	static const char cname[] = "CALLSIGN";
	size_t pos = 0;
	size_t sdes_start;
	size_t words;

	memset(out, 0, TEST_PACKET_LEN);

	/* Receiver report without report blocks: header + SSRC */
	out[pos++] = (uint8_t)(version << 6);
	out[pos++] = 201;
	out[pos++] = 0;
	out[pos++] = 1;
	pos += 4;

	/* SDES with one chunk */
	sdes_start = pos;
	out[pos++] = (uint8_t)((version << 6) | 1);
	out[pos++] = 202;
	pos += 2; /* length, filled in below */
	pos += 4; /* SSRC */

	out[pos++] = 1; /* CNAME */
	out[pos++] = (uint8_t)(sizeof(cname) - 1);
	memcpy(&out[pos], cname, sizeof(cname) - 1);
	pos += sizeof(cname) - 1;

	out[pos++] = 2; /* NAME */
	out[pos++] = (uint8_t)name_len;
	memcpy(&out[pos], name, name_len);
	pos += name_len;

	out[pos++] = 0; /* END */
	while ((pos - sdes_start) % 4 != 0)
		out[pos++] = 0;

	words = (pos - sdes_start) / 4 - 1;
	out[sdes_start + 2] = (uint8_t)(words >> 8);
	out[sdes_start + 3] = (uint8_t)(words & 0xFF);

	return pos;
}

static int expect(const char *what, int got, int expected)
{
	if (got != expected) {
		fprintf(stderr, "Error: %s: expected %d, got %d\n",
			what, expected, got);
		return -EINVAL;
	}

	return 0;
}

static int test_ndata(void)
{
	static const uint8_t info[] = "oNDATA\rStation info\n";
	static const uint8_t chat[] = "oNDATAhello there";
	static const uint8_t info_no_nul[] = { 'o', 'N', 'D', 'A', 'T', 'A',
					       0x0d, 'x', 'y' };
	static const uint8_t info_nul_first[] = { 0, 'N', 'D', 'A', 'T', 'A',
						  0x0d, 'x', 0 };
	static const uint8_t chat_nul_first[] = { 0, 'N', 'D', 'A', 'T', 'A',
						  'x', 0 };
	static const uint8_t short_pkt[] = { 'o', 'N', 'D' };
	static const uint8_t audio_short[] = { 0xc0, 0x01 };
	static const uint8_t other[] = "oSOMETHING";
	int ret = 0;

	/* sizeof() includes the terminating NUL of the string literals */
	ret |= expect("valid NDATA info packet",
		      packet_filter_ndata_malformed(info, sizeof(info)), 0);
	ret |= expect("valid NDATA chat packet",
		      packet_filter_ndata_malformed(chat, sizeof(chat)), 0);
	ret |= expect("NDATA info packet without NUL",
		      packet_filter_ndata_malformed(info_no_nul,
						    sizeof(info_no_nul)), 1);
	ret |= expect("NDATA info packet with NUL before its body",
		      packet_filter_ndata_malformed(info_nul_first,
						    sizeof(info_nul_first)), 1);
	ret |= expect("NDATA chat packet with NUL before its body",
		      packet_filter_ndata_malformed(chat_nul_first,
						    sizeof(chat_nul_first)), 1);
	ret |= expect("packet shorter than 7 bytes",
		      packet_filter_ndata_malformed(short_pkt,
						    sizeof(short_pkt)), 1);
	ret |= expect("short audio packet",
		      packet_filter_ndata_malformed(audio_short,
						    sizeof(audio_short)), 0);
	ret |= expect("other non-audio packet",
		      packet_filter_ndata_malformed(other, sizeof(other)), 0);

	return ret;
}

static int test_sdes(void)
{
	uint8_t pkt[TEST_PACKET_LEN];
	char name[300];
	size_t len;
	int ret = 0;

	len = build_rtcp(pkt, 3, "DB9CR-L Christian", 17);
	ret |= expect("regular callsign and name",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	len = build_rtcp(pkt, 3, "*ECHOTEST* Test server", 22);
	ret |= expect("conference callsign",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	len = build_rtcp(pkt, 1, "DB9CR/P Name", 12);
	ret |= expect("version 1 packet with portable callsign",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	len = build_rtcp(pkt, 3, "DB9CR[exec\trm] x", 16);
	ret |= expect("TCL command substitution in callsign",
		      packet_filter_sdes_unsafe(pkt, len), 1);

	len = build_rtcp(pkt, 1, "AB;CD Name", 10);
	ret |= expect("invalid character, version 1 packet",
		      packet_filter_sdes_unsafe(pkt, len), 1);

	len = build_rtcp(pkt, 3, "  [x] y", 7);
	ret |= expect("unsafe callsign after leading whitespace",
		      packet_filter_sdes_unsafe(pkt, len), 1);

	len = build_rtcp(pkt, 3, "DB9CR [whatever] name", 21);
	ret |= expect("unusual characters only in the name",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	len = build_rtcp(pkt, 3, "DB9CR\0[x] name", 14);
	ret |= expect("unsafe text after an embedded NUL",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	memset(name, 'A', 32);
	memcpy(&name[32], " Name", 5);
	len = build_rtcp(pkt, 3, name, 37);
	ret |= expect("callsign of the maximum length",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	memset(name, 'A', 33);
	memcpy(&name[33], " Name", 5);
	len = build_rtcp(pkt, 3, name, 38);
	ret |= expect("callsign longer than the maximum",
		      packet_filter_sdes_unsafe(pkt, len), 1);

	memset(name, 'A', 255);
	len = build_rtcp(pkt, 3, name, 255);
	ret |= expect("255 byte callsign without a name",
		      packet_filter_sdes_unsafe(pkt, len), 1);

	len = build_rtcp(pkt, 3, "[x] y", 5);
	ret |= expect("truncated packet (NAME item cut off)",
		      packet_filter_sdes_unsafe(pkt, len - 8), 0);

	len = build_rtcp(pkt, 3, "[x] y", 5);
	pkt[0] = 0x80; /* RTP version 2: not an EchoLink RTCP packet */
	ret |= expect("wrong RTP version",
		      packet_filter_sdes_unsafe(pkt, len), 0);

	ret |= expect("empty packet", packet_filter_sdes_unsafe(pkt, 0), 0);

	return ret;
}

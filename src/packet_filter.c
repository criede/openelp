/*!
 * @file packet_filter.c
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
 */

#include <stdlib.h>
#include <string.h>

#include "packet_filter.h"

/*! RTCP packet type of a source description (SDES) packet */
#define RTCP_SDES 202

/*! SDES item type terminating the item list */
#define RTCP_SDES_END 0

/*! SDES item type of the NAME item, which carries the EchoLink callsign */
#define RTCP_SDES_NAME 2

/*! RTCP packet type of a sender report */
#define RTCP_SR 200

/*! RTCP packet type of a receiver report */
#define RTCP_RR 201

/*! RTCP packet type of a goodbye (BYE) packet */
#define RTCP_BYE 203

/*! First byte value marking an EchoLink audio packet */
#define AUDIO_PREFIX 0xc0

/*! Payload type (second byte) of a Speex audio packet */
#define AUDIO_PT_SPEEX 0x96

/*! Size of the RTP header preceding the audio payload */
#define AUDIO_HEADER_LEN 12

/*! Maximum audio payload size accepted (VoicePacket::data) */
#define AUDIO_MAX_PAYLOAD 1024

/*! Number of GSM frames expected in a GSM audio packet */
#define AUDIO_GSM_FRAMES 4

/*! Size of one GSM frame */
#define AUDIO_GSM_FRAME_LEN 33

/*!
 * @brief Locate the NAME item of the first SDES subpacket in an RTCP packet
 *
 * Walks a (possibly compound) RTCP packet exactly like SvxLink's
 * parseSDES(): subpackets must have version 3 (EchoLink) or 1 and lie
 * entirely within the datagram; only the first SDES subpacket with a
 * non-zero source count is searched, and only its first chunk.
 *
 * @param[in] buf Packet payload
 * @param[in] len Number of valid bytes in @p buf
 * @param[out] item_len Length of the NAME item body
 *
 * @returns Pointer to the NAME item body, or NULL if there is none
 */
static const uint8_t *find_sdes_name(const uint8_t *buf, size_t len,
				     size_t *item_len);

/*!
 * @brief Determine if a character is whitespace as SvxLink sees it
 *
 * @param[in] ch Character to test
 *
 * @returns 1 for ' ', '\\t', '\\n' or '\\r', 0 otherwise
 */
static int is_sdes_space(uint8_t ch);

/*!
 * @brief Determine if a character is valid in an EchoLink callsign
 *
 * @param[in] ch Character to test
 *
 * @returns 1 if SvxLink's sanitizeCallsign() would keep it, 0 otherwise
 */
static int is_callsign_char(uint8_t ch);

static const uint8_t *find_sdes_name(const uint8_t *buf, size_t len,
				     size_t *item_len)
{
	size_t pos = 0;

	while (len - pos >= 4 &&
	       (((buf[pos] >> 6) & 3) == 3 || ((buf[pos] >> 6) & 3) == 1)) {
		size_t sub_len = (((size_t)buf[pos + 2] << 8) |
				  (size_t)buf[pos + 3]) + 1;

		sub_len *= 4;

		/* The subpacket must lie entirely within the datagram */
		if (sub_len > len - pos)
			break;

		if (buf[pos + 1] == RTCP_SDES && (buf[pos] & 0x1F) > 0) {
			/* Items start after the header and the SSRC */
			size_t cp = pos + 8;
			size_t lp = pos + sub_len;

			while (cp + 2 <= lp) {
				uint8_t itype = buf[cp];
				size_t ilen = buf[cp + 1];

				if (itype == RTCP_SDES_END)
					break;

				/* The item body must fit within the subpacket */
				if (cp + 2 + ilen > lp)
					break;

				if (itype == RTCP_SDES_NAME) {
					*item_len = ilen;
					return &buf[cp + 2];
				}

				cp += ilen + 2;
			}

			/* SvxLink only looks at the first SDES subpacket */
			break;
		}

		pos += sub_len;
	}

	return NULL;
}

static int is_sdes_space(uint8_t ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int is_callsign_char(uint8_t ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
	       (ch >= '0' && ch <= '9') ||
	       ch == '-' || ch == '/' || ch == '*' || ch == '_';
}

int packet_filter_ndata_malformed(const uint8_t *buf, size_t len)
{
	const uint8_t *null;

	/* Audio packets are not affected */
	if (len > 0 && buf[0] == 0xc0)
		return 0;

	/* Too short for prefix byte + "NDATA" + sub-type byte */
	if (len < 7)
		return 1;

	/* Not an NDATA packet */
	if (memcmp(buf + 1, "NDATA", 5) != 0)
		return 0;

	null = memchr(buf, 0, len);

	/* Info packet: body starts at offset 7 */
	if (buf[6] == 0x0d)
		return null == NULL || null < buf + 7;

	/* Chat packet: body starts at offset 6 */
	return null == NULL || null < buf + 6;
}

int packet_filter_sdes_unsafe(const uint8_t *buf, size_t len)
{
	const uint8_t *name;
	size_t name_len = 0;
	size_t i = 0;
	size_t call_len = 0;

	name = find_sdes_name(buf, len, &name_len);
	if (name == NULL)
		return 0;

	/* SvxLink copies the item into a C string, so it ends at a NUL */
	{
		const uint8_t *nul = memchr(name, 0, name_len);

		if (nul != NULL)
			name_len = (size_t)(nul - name);
	}

	/* The callsign is the first whitespace-separated token (strtok_r()
	 * in the Dispatcher skips leading whitespace; the Qso takes the text
	 * before the first whitespace, which is the same token or empty)
	 */
	while (i < name_len && is_sdes_space(name[i]))
		i++;

	for (; i < name_len && !is_sdes_space(name[i]); i++) {
		if (!is_callsign_char(name[i]))
			return 1;

		if (++call_len > PACKET_FILTER_MAX_CALLSIGN_LEN)
			return 1;
	}

	return 0;
}

int packet_filter_data_invalid(const uint8_t *buf, size_t len)
{
	/* Audio packets carry the 0xc0 prefix */
	if (len > 0 && buf[0] == AUDIO_PREFIX) {
		if (len < AUDIO_HEADER_LEN)
			return 1;
		if (len > (size_t)AUDIO_HEADER_LEN + AUDIO_MAX_PAYLOAD)
			return 1;
		/* Anything but Speex is decoded as a fixed number of GSM frames */
		if (buf[1] != AUDIO_PT_SPEEX &&
		    len < (size_t)AUDIO_HEADER_LEN +
			  AUDIO_GSM_FRAMES * AUDIO_GSM_FRAME_LEN)
			return 1;
		return 0;
	}

	/* The only other packets that are acted on are NDATA packets */
	if (len < 7)
		return 1;
	if (memcmp(buf + 1, "NDATA", 5) != 0)
		return 1;

	return packet_filter_ndata_malformed(buf, len);
}

int packet_filter_control_invalid(const uint8_t *buf, size_t len)
{
	size_t pos = 0;
	int version;

	if (len < 4)
		return 1;

	/* The first subpacket must be a report without padding */
	version = (buf[0] >> 6) & 3;
	if (version != 3 && version != 1)
		return 1;
	if ((buf[0] & 0x20) != 0)
		return 1;
	if (buf[1] != RTCP_SR && buf[1] != RTCP_RR)
		return 1;

	/* Forward only if an SDES or BYE subpacket is present */
	while (len - pos >= 4) {
		size_t sub_len;

		version = (buf[pos] >> 6) & 3;
		if (version != 3 && version != 1)
			break;

		sub_len = (((((size_t)buf[pos + 2]) << 8) |
			    (size_t)buf[pos + 3]) + 1) * 4;
		if (sub_len > len - pos)
			break;

		if (buf[pos + 1] == RTCP_SDES || buf[pos + 1] == RTCP_BYE)
			return 0;

		pos += sub_len;
	}

	return 1;
}

/*! Parser states of the directory listing filter */
enum {
	DIR_START = 0,
	DIR_COUNT,
	DIR_CALL,
	DIR_DATA,
	DIR_ID,
	DIR_IP,
	DIR_PASS
};

/*! Bytes that introduce a directory listing */
static const uint8_t dir_start_marker[4] = { '@', '@', '@', '\n' };

/*!
 * @brief Emit the current callsign line, rewritten unless it is a marker
 *
 * @param[in,out] f Filter state
 * @param[out] out Buffer receiving the line and its terminating newline
 *
 * @returns Number of bytes written to @p out
 */
static size_t dir_emit_call(struct packet_filter_dir *f, uint8_t *out)
{
	size_t o = 0;
	size_t i;
	size_t call_len = 0;

	f->call_dot = (f->line_len == 1 && f->line[0] == '.') ? 1 : 0;

	if (f->line_len == 0 ||
	    (f->line_len == 1 && (f->line[0] == '.' || f->line[0] == ' '))) {
		memcpy(out, f->line, f->line_len);
		o = f->line_len;
	} else {
		for (i = 0; i < f->line_len &&
			    call_len < PACKET_FILTER_MAX_CALLSIGN_LEN; i++) {
			if (is_callsign_char(f->line[i])) {
				out[o++] = f->line[i];
				call_len++;
			}
		}
	}

	out[o++] = '\n';

	return o;
}

void packet_filter_dir_init(struct packet_filter_dir *f)
{
	f->state = DIR_START;
	f->start_len = 0;
	f->remaining = 0;
	f->line_len = 0;
	f->call_dot = 0;
}

size_t packet_filter_dir_process(struct packet_filter_dir *f,
				 const uint8_t *in, size_t in_len,
				 uint8_t *out)
{
	size_t i;
	size_t o = 0;

	for (i = 0; i < in_len; i++) {
		uint8_t ch = in[i];

		switch (f->state) {
		case DIR_PASS:
			out[o++] = ch;
			break;

		case DIR_START:
			out[o++] = ch;
			if (ch == dir_start_marker[f->start_len]) {
				f->start_len++;
				if (f->start_len == sizeof(dir_start_marker)) {
					f->state = DIR_COUNT;
					f->line_len = 0;
				}
			} else {
				f->state = DIR_PASS;
			}
			break;

		case DIR_COUNT:
			out[o++] = ch;
			if (ch == '\n') {
				f->line[f->line_len] = 0;
				f->remaining =
					strtol((char *)f->line, NULL, 10);
				f->line_len = 0;
				f->call_dot = 0;
				f->state = f->remaining > 0 ?
					   DIR_CALL : DIR_PASS;
			} else if (f->line_len < PACKET_FILTER_DIR_LINE_LEN - 1) {
				f->line[f->line_len++] = ch;
			} else {
				f->state = DIR_PASS;
			}
			break;

		case DIR_CALL:
			if (ch == '\n') {
				o += dir_emit_call(f, out + o);
				f->line_len = 0;
				f->state = DIR_DATA;
			} else if (f->line_len < PACKET_FILTER_DIR_LINE_LEN - 1) {
				f->line[f->line_len++] = ch;
			} else {
				memcpy(out + o, f->line, f->line_len);
				o += f->line_len;
				out[o++] = ch;
				f->line_len = 0;
				f->state = DIR_PASS;
			}
			break;

		case DIR_DATA:
			out[o++] = ch;
			if (ch == '\n')
				f->state = DIR_ID;
			break;

		case DIR_ID:
			out[o++] = ch;
			if (ch == '\n')
				f->state = DIR_IP;
			break;

		case DIR_IP:
			out[o++] = ch;
			if (ch == '\n') {
				if (f->call_dot) {
					f->state = DIR_CALL;
				} else {
					f->remaining--;
					f->state = f->remaining <= 0 ?
						   DIR_PASS : DIR_CALL;
				}
				f->call_dot = 0;
			}
			break;

		default:
			out[o++] = ch;
			f->state = DIR_PASS;
			break;
		}
	}

	return o;
}

size_t packet_filter_dir_flush(struct packet_filter_dir *f, uint8_t *out)
{
	size_t o = 0;

	if (f->state == DIR_CALL && f->line_len > 0) {
		memcpy(out, f->line, f->line_len);
		o = f->line_len;
		f->line_len = 0;
	}

	return o;
}

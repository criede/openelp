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

#include <string.h>

#include "packet_filter.h"

/*! RTCP packet type of a source description (SDES) packet */
#define RTCP_SDES 202

/*! SDES item type terminating the item list */
#define RTCP_SDES_END 0

/*! SDES item type of the NAME item, which carries the EchoLink callsign */
#define RTCP_SDES_NAME 2

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

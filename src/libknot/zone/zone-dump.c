/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/descriptor_new.h"
#include <config.h>

#include "common/print.h"		// hex_printf
#include "libknot/dname.h"

#include <ctype.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libknot/libknot.h"
#include "libknot/common.h"
#include "common/skip-list.h"
#include "common/base32hex.h"

#ifdef GRR
/*!< \todo #1683 Find all maximum lengths to be used in strcnat. */

enum uint_max_length {
	U8_MAX_STR_LEN = 4, U16_MAX_STR_LEN = 6,
	U32_MAX_STR_LEN = 11, MAX_RR_TYPE_LEN = 20,
	MAX_NSEC_BIT_STR_LEN = 4096,
	};

#define APL_NEGATION_MASK      0x80U
#define APL_LENGTH_MASK	       (~APL_NEGATION_MASK)

/* RFC 4025 - codes for different types that IPSECKEY can hold. */
#define IPSECKEY_NOGATEWAY 0
#define IPSECKEY_IP4 1
#define IPSECKEY_IP6 2
#define IPSECKEY_DNAME 3

/* Following copyrights are only valid for b64_ntop function */
/*
 * Copyright (c) 1996, 1998 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Portions Copyright (c) 1995 by International Business Machines, Inc.
 *
 * International Business Machines, Inc. (hereinafter called IBM) grants
 * permission under its copyrights to use, copy, modify, and distribute this
 * Software with or without fee, provided that the above copyright notice and
 * all paragraphs of this notice appear in all copies, and that the name of IBM
 * not be used in connection with the marketing of any product incorporating
 * the Software or modifications thereof, without specific, written prior
 * permission.
 *
 * To the extent it has a right to do so, IBM grants an immunity from suit
 * under its patents, if any, for the use, sale or manufacture of products to
 * the extent that such products are used for performing Domain Name System
 * dynamic updates in TCP/IP networks by means of the Software.  No immunity is
 * granted for any product per se or for any other function of any product.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", AND IBM DISCLAIMS ALL WARRANTIES,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL IBM BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE, EVEN
 * IF IBM IS APPRISED OF THE POSSIBILITY OF SUCH DAMAGES.
 */
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Assert(Cond) if (!(Cond)) abort()

static const char Base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char Pad64 = '=';

/* (From RFC1521 and draft-ietf-dnssec-secext-03.txt)
   The following encoding technique is taken from RFC 1521 by Borenstein
   and Freed.  It is reproduced here in a slightly edited form for
   convenience.

   A 65-character subset of US-ASCII is used, enabling 6 bits to be
   represented per printable character. (The extra 65th character, "=",
   is used to signify a special processing function.)

   The encoding process represents 24-bit groups of input bits as output
   strings of 4 encoded characters. Proceeding from left to right, a
   24-bit input group is formed by concatenating 3 8-bit input groups.
   These 24 bits are then treated as 4 concatenated 6-bit groups, each
   of which is translated into a single digit in the base64 alphabet.

   Each 6-bit group is used as an index into an array of 64 printable
   characters. The character referenced by the index is placed in the
   output string.

                         Table 1: The Base64 Alphabet

      Value Encoding  Value Encoding  Value Encoding  Value Encoding
          0 A            17 R            34 i            51 z
          1 B            18 S            35 j            52 0
          2 C            19 T            36 k            53 1
          3 D            20 U            37 l            54 2
          4 E            21 V            38 m            55 3
          5 F            22 W            39 n            56 4
          6 G            23 X            40 o            57 5
          7 H            24 Y            41 p            58 6
          8 I            25 Z            42 q            59 7
          9 J            26 a            43 r            60 8
         10 K            27 b            44 s            61 9
         11 L            28 c            45 t            62 +
         12 M            29 d            46 u            63 /
         13 N            30 e            47 v
         14 O            31 f            48 w         (pad) =
         15 P            32 g            49 x
         16 Q            33 h            50 y

   Special processing is performed if fewer than 24 bits are available
   at the end of the data being encoded.  A full encoding quantum is
   always completed at the end of a quantity.  When fewer than 24 input
   bits are available in an input group, zero bits are added (on the
   right) to form an integral number of 6-bit groups.  Padding at the
   end of the data is performed using the '=' character.

   Since all base64 input is an integral number of octets, only the
         -------------------------------------------------
   following cases can arise:

       (1) the final quantum of encoding input is an integral
           multiple of 24 bits; here, the final unit of encoded
	   output will be an integral multiple of 4 characters
	   with no "=" padding,
       (2) the final quantum of encoding input is exactly 8 bits;
           here, the final unit of encoded output will be two
	   characters followed by two "=" padding characters, or
       (3) the final quantum of encoding input is exactly 16 bits;
           here, the final unit of encoded output will be three
	   characters followed by one "=" padding character.
   */

int b64_ntop(uint8_t const *src, size_t srclength, char *target,
	     size_t targsize) {
	size_t datalength = 0;
	uint8_t input[3];
	uint8_t output[4];
	size_t i;

	while (2 < srclength) {
		input[0] = *src++;
		input[1] = *src++;
		input[2] = *src++;
		srclength -= 3;

		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
		output[3] = input[2] & 0x3f;
		Assert(output[0] < 64);
		Assert(output[1] < 64);
		Assert(output[2] < 64);
		Assert(output[3] < 64);

		if (datalength + 4 > targsize)
			return (-1);
		target[datalength++] = Base64[output[0]];
		target[datalength++] = Base64[output[1]];
		target[datalength++] = Base64[output[2]];
		target[datalength++] = Base64[output[3]];
	}

	/* Now we worry about padding. */
	if (0 != srclength) {
		/* Get what's left. */
		input[0] = input[1] = input[2] = '\0';
		for (i = 0; i < srclength; i++)
			input[i] = *src++;

		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
		Assert(output[0] < 64);
		Assert(output[1] < 64);
		Assert(output[2] < 64);

		if (datalength + 4 > targsize)
			return (-1);
		target[datalength++] = Base64[output[0]];
		target[datalength++] = Base64[output[1]];
		if (srclength == 1)
			target[datalength++] = Pad64;
		else
			target[datalength++] = Base64[output[2]];
		target[datalength++] = Pad64;
	}
	if (datalength >= targsize)
		return (-1);
	target[datalength] = '\0';	/* Returned value doesn't count \0. */
	return (datalength);
}

/* Taken from RFC 4398, section 2.1.  */
knot_lookup_table_t knot_dns_certificate_types[] = {
/*	0		Reserved */
	{ 1, "PKIX" },	/* X.509 as per PKIX */
	{ 2, "SPKI" },	/* SPKI cert */
	{ 3, "PGP" },	/* OpenPGP packet */
	{ 4, "IPKIX" },	/* The URL of an X.509 data object */
	{ 5, "ISPKI" },	/* The URL of an SPKI certificate */
	{ 6, "IPGP" },	/* The fingerprint and URL of an OpenPGP packet */
	{ 7, "ACPKIX" },	/* Attribute Certificate */
	{ 8, "IACPKIX" },	/* The URL of an Attribute Certificate */
	{ 253, "URI" },	/* URI private */
	{ 254, "OID" },	/* OID private */
/*	255 		Reserved */
/* 	256-65279	Available for IANA assignment */
/*	65280-65534	Experimental */
/*	65535		Reserved */
	{ 0, NULL }
};

/* Taken from RFC 2535, section 7.  */
knot_lookup_table_t knot_dns_algorithms[] = {
	{ 1, "RSAMD5" },	/* RFC 2537 */
	{ 2, "DH" },		/* RFC 2539 */
	{ 3, "DSA" },		/* RFC 2536 */
	{ 4, "ECC" },
	{ 5, "RSASHA1" },	/* RFC 3110 */
	{ 252, "INDIRECT" },
	{ 253, "PRIVATEDNS" },
	{ 254, "PRIVATEOID" },
	{ 0, NULL }
};

static int get_bit(uint8_t bits[], size_t index)
{
	/*
	 * The bits are counted from left to right, so bit #0 is the
	 * left most bit.
	 */
	return bits[index / 8] & (1 << (7 - index % 8));
}

static inline uint8_t * rdata_item_data(knot_rdata_item_t item)
{
	return (uint8_t *)(item.raw_data + 1);
}

static inline uint16_t rdata_item_size(knot_rdata_item_t item)
{
	return item.raw_data[0];
}

static char *rdata_dname_to_string(knot_rdata_item_t item)
{
	return knot_dname_to_str(item.dname);
}

static char *rdata_binary_dname_to_string(knot_rdata_item_t item)
{
	if (item.raw_data == NULL) {
		return NULL;
	}
	
	if (item.raw_data[0] == 0) {
		return NULL;
	}
	
	/* Create new dname frow raw data - probably the easiest way. */
	knot_dname_t *dname = knot_dname_new_from_wire((uint8_t *)(item.raw_data + 1),
	                                               item.raw_data[0], NULL);
	if (dname == NULL) {
		return NULL;
	}
	
	/* Create string. */
	char *str = knot_dname_to_str(dname);
	knot_dname_free(&dname);
	
	return str;
}

static char *rdata_dns_name_to_string(knot_rdata_item_t item)
{
	return knot_dname_to_str(item.dname);
}

static char *rdata_txt_data_to_string(const uint8_t *data, uint32_t *count)
{
    uint8_t length = data[0];
    uint32_t i = 0;
    /*
     * * 4 because: unprintable chars look like \123.
     * + 3 because: opening '"', closing '"' and \0 at the end.
     * + 1 because: malloc + gcc optimization requires out_length >= 4.
     *
     * NOTE: length can be 0.
     */
    uint32_t out_length = length * 4 + 3 + 1;

    char *ret = malloc(out_length);
    if (ret == NULL) {
            ERR_ALLOC_FAILED;
            return NULL;
    }
    memset(ret, 0, out_length);

    // Opening '"'
    strcat(ret, "\"");

    for (i = 1; i <= length; i++) {
            char ch = (char) data[i];
            char tmp_str[5];

            if (isprint((int)ch)) {
                    if (ch == '"' || ch == '\\') {
                            strcat(ret, "\\");
                    }
                    tmp_str[0] = ch;
                    tmp_str[1] = 0;
                    strcat(ret, tmp_str);
            } else {
                    sprintf(tmp_str, "\\%03u", ch);
                    strcat(ret, tmp_str);
            }
    }

    // Closing '"'
    strcat(ret, "\"");

    *count = length + 1; // 1 - leading length byte.

    return ret;
}

static char *rdata_text_array_to_string(knot_rdata_item_t item)
{
    /* Create delimiter. */
    char *del = " ";

    uint16_t size = item.raw_data[0];
    /*
     * * 6 because: item can consists of one length unprintable char strings
     *              "\123" "\123" ...
     * + 1 because: ending \0.
     *
     * NOTE: txt_size is always bigger than 4 bytes (zero string has size = 1).
     */
    uint32_t txt_size = size * 6 + 1;

    char *ret = malloc(txt_size);
    if (ret == NULL) {
            ERR_ALLOC_FAILED;
            return NULL;
    }
    memset(ret, 0, txt_size);

    const uint8_t *data = (uint8_t *)(item.raw_data + 1);
    uint32_t read_count = 0;

    while (read_count < size) {
            uint32_t txt_count = 0;

            char *txt = rdata_txt_data_to_string(data + read_count, &txt_count);
            if (txt == NULL) {
                    free(ret);
                    return NULL;
            }

            /* Append text string to output. */
            strcat(ret, txt);
            read_count += txt_count;

            if (read_count < size) {
                    strcat(ret, del);
            }

            free(txt);
    }

    return ret;
}

static char *rdata_byte_to_string(knot_rdata_item_t item)
{
	assert(item.raw_data[0] == 1);
	uint8_t data = *((uint8_t *)(item.raw_data + 1));
	char *ret = malloc(sizeof(char) * U8_MAX_STR_LEN);
	snprintf(ret, U8_MAX_STR_LEN, "%d", (char) data);
	return ret;
}

static char *rdata_short_to_string(knot_rdata_item_t item)
{
	uint16_t data = knot_wire_read_u16(rdata_item_data(item));
	char *ret = malloc(sizeof(char) * U16_MAX_STR_LEN);
	snprintf(ret, U16_MAX_STR_LEN, "%u", data);
	/* XXX Use proper macros - see response tests*/
	/* XXX check return value, return NULL on failure */
	return ret;
}

static char *rdata_long_to_string(knot_rdata_item_t item)
{
	uint32_t data = knot_wire_read_u32(rdata_item_data(item));
	char *ret = malloc(sizeof(char) * U32_MAX_STR_LEN);
	/* u should be enough */
	snprintf(ret, U32_MAX_STR_LEN, "%u", data);
	return ret;
}

static char *rdata_a_to_string(knot_rdata_item_t item)
{
	/* 200 seems like a little too much */
	char *ret = malloc(sizeof(char) * 200);
	if (inet_ntop(AF_INET, rdata_item_data(item), ret, 200)) {
		return ret;
	} else {
		return NULL;
	}
}

static char *rdata_aaaa_to_string(knot_rdata_item_t item)
{
	char *ret = malloc(sizeof(char) * 200);
	if (inet_ntop(AF_INET6, rdata_item_data(item), ret, 200)) {
		return ret;
	} else {
		return NULL;
	}
}

static char *rdata_rrtype_to_string(knot_rdata_item_t item)
{
	char buff[32];

	uint16_t type = knot_wire_read_u16(rdata_item_data(item));

	if (knot_rrtype_to_string(type, buff, sizeof(buff)) < 0)
		return NULL;

	char *ret = malloc(sizeof(char) * MAX_RR_TYPE_LEN);
	strncpy(ret, buff, MAX_RR_TYPE_LEN);
	return ret;
}

static char *rdata_algorithm_to_string(knot_rdata_item_t item)
{
	uint8_t id = *rdata_item_data(item);
	char *ret = malloc(sizeof(char) * MAX_RR_TYPE_LEN);
	knot_lookup_table_t *alg
		= knot_lookup_by_id(knot_dns_algorithms, id);
	if (alg) {
		strncpy(ret, alg->name, MAX_RR_TYPE_LEN);
	} else {
		snprintf(ret, U8_MAX_STR_LEN, "%u", id);
	}

	return ret;
}

static char *rdata_certificate_type_to_string(knot_rdata_item_t item)
{
	uint16_t id = knot_wire_read_u16(rdata_item_data(item));
	char *ret = malloc(sizeof(char) * MAX_RR_TYPE_LEN);
	knot_lookup_table_t *type
		= knot_lookup_by_id(knot_dns_certificate_types, id);
	if (type) {
		strncpy(ret, type->name, MAX_RR_TYPE_LEN);
	} else {
		snprintf(ret, U16_MAX_STR_LEN, "%u", id);
	}

	return ret;
}

static char *rdata_period_to_string(knot_rdata_item_t item)
{
	/* uint32 but read 16 XXX */
	uint32_t period = knot_wire_read_u32(rdata_item_data(item));
	char *ret = malloc(sizeof(char) * U32_MAX_STR_LEN);
	snprintf(ret, U32_MAX_STR_LEN, "%u", period);
	return ret;
}

static char *rdata_time_to_string(knot_rdata_item_t item)
{
	time_t time = (time_t) knot_wire_read_u32(rdata_item_data(item));
	struct tm tm_conv;
	if (gmtime_r(&time, &tm_conv) == 0) {
		return 0;
	}
	char *ret = malloc(sizeof(char) * 15);
	if (strftime(ret, 15, "%Y%m%d%H%M%S", &tm_conv)) {
		return ret;
	} else {
		free(ret);
		return 0;
	}
}

static char *rdata_base32_to_string(knot_rdata_item_t item)
{
	char *ret = NULL;
	size_t size = rdata_item_size(item);

	if (size == 0) {
		ret = malloc(2);

		ret[0] = '-';
		ret[1] = '\0';
	} else {
		int32_t  b32_out;
		uint32_t out_len = ((size + 4) / 5) * 8;

		ret = malloc(out_len + 1);

		b32_out = base32hex_encode(rdata_item_data(item) + 1,
					   size - 1,
					   (uint8_t *)ret,
					   out_len);
		if (b32_out <= 0) {
			free(ret);
			return NULL;
		}

		ret[b32_out] = '\0';
	}

	return ret;
}

/*!< \todo Replace with function from .../common after release. */
static char *rdata_base64_to_string(knot_rdata_item_t item)
{
	int length;
	size_t size = rdata_item_size(item);
	char *ret = malloc((sizeof(char) * 2 * size) + 1 * sizeof(char));
	length = b64_ntop(rdata_item_data(item), size,
			  ret, (sizeof(char)) * (size * 2 + 1));
	if (length > 0) {
		return ret;
	} else {
		free(ret);
		return NULL;
	}
}

static char *knot_hex_to_string(const uint8_t *data, size_t size)
{
	static const char hexdigits[] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
	};
	size_t i;

	char *ret = malloc(sizeof(char) * (size * 2 + 1));

	for (i = 0; i < size * 2; i += 2) {
		uint8_t octet = *data++;
		ret[i] = hexdigits [octet >> 4];
		ret[i + 1] = hexdigits [octet & 0x0f];
	}

	ret[i] = '\0';

	return ret;
}

char *rdata_hex_to_string(knot_rdata_item_t item)
{
	return knot_hex_to_string(rdata_item_data(item), rdata_item_size(item));
}

char *rdata_hexlen_to_string(knot_rdata_item_t item)
{
	if(rdata_item_size(item) <= 1) {
		// NSEC3 salt hex can be empty
		char *ret = malloc(sizeof(char) * 2);
		ret[0] = '-';
		ret[1] = '\0';
		return ret;
	} else {
		return knot_hex_to_string(rdata_item_data(item) + 1,
		                          rdata_item_size(item) - 1);
	}
}

char *rdata_nsap_to_string(knot_rdata_item_t item)
{
	char *ret = malloc(sizeof(char) * ((rdata_item_size(item) * 2) + 7));
	if (ret == NULL) {
		ERR_ALLOC_FAILED;
		return NULL;
	}

	memset(ret, 0, sizeof(char) * ((rdata_item_size(item) * 2) + 7));

	/* String is already terminated. */
	memcpy(ret, "0x", strlen("0x"));
	char *converted = knot_hex_to_string(rdata_item_data(item),
	                                     rdata_item_size(item));
	if (converted == NULL) {
		free(ret);
		return NULL;
	}

	strncat(ret, converted, rdata_item_size(item) * 2 + 1);
	free(converted);
	return ret;
}

char *rdata_apl_to_string(knot_rdata_item_t item)
{
	uint8_t *data = rdata_item_data(item);
	size_t read = 0;
	char *pos = malloc(sizeof(char) * MAX_NSEC_BIT_STR_LEN);
	if (pos == NULL) {
		ERR_ALLOC_FAILED;
		return NULL;
	}
	memset(pos, 0, MAX_NSEC_BIT_STR_LEN);
	
	char *ret_base = pos;
	
	while (read < rdata_item_size(item)) {
		uint16_t address_family = knot_wire_read_u16(data);
		uint8_t prefix = data[2];
		uint8_t length = data[3];
		int negated = length & APL_NEGATION_MASK;
		int af = -1;

		length &= APL_LENGTH_MASK;
		switch (address_family) {
			case 1: af = AF_INET; break;
			case 2: af = AF_INET6; break;
		}

		if (af != -1) {
			char text_address[1024];
			memset(text_address, 0, sizeof(text_address));
			uint8_t address[128];
			memset(address, 0, sizeof(address));
			memcpy(address, data + 4, length);
			/* Only valid data should be present here. */
			assert((data + 4) - rdata_item_data(item) <= rdata_item_size(item));
			/* Move pointer at the end of already written data. */
			pos = ret_base + strlen(ret_base);
			if (inet_ntop(af, address,
				      text_address,
				      sizeof(text_address))) {
				snprintf(pos, sizeof(text_address) +
					 U32_MAX_STR_LEN * 2,
					 "%s%d:%s/%d%s",
					 negated ? "!" : "",
					 (int) address_family,
					 text_address,
					 (int) prefix,
					 /* Last item should not have trailing space. */
					 (read + 4 + length < rdata_item_size(item))
					 ? " " : "");
			}
		}

		data += 4 + length;
		read += 4 + length;
	}

	return ret_base;
}

char *rdata_services_to_string(knot_rdata_item_t item)
{
	uint8_t *data = rdata_item_data(item);
	uint8_t protocol_number = data[0];
	ssize_t bitmap_size = rdata_item_size(item) - 1;
	uint8_t *bitmap = data + 1;
	struct protoent *proto = getprotobynumber(protocol_number);

	char *ret = malloc(sizeof(char) * MAX_NSEC_BIT_STR_LEN);

	memset(ret, 0, MAX_NSEC_BIT_STR_LEN);

	if (proto) {
		int i;

		strncpy(ret, proto->p_name, strlen(proto->p_name));
		strncat(ret, " ", 2);

		for (i = 0; i < bitmap_size * 8; ++i) {
			if (get_bit(bitmap, i)) {
				struct servent *service =
					getservbyport((int)htons(i),
						      proto->p_name);
				if (service) {
					strncat(ret, service->s_name,
					        strlen(service->s_name));
					strncat(ret, " ", 2);
				} else {
					char tmp[U32_MAX_STR_LEN];
					snprintf(tmp, U32_MAX_STR_LEN,
						 "%d ", i);
					strncat(ret, tmp, U32_MAX_STR_LEN);
				}
			}
		}
	}

	return ret;
}

char *rdata_ipsecgateway_to_string(knot_rdata_item_t item,
                                   const knot_rrset_t *rrset)
{
	const knot_rdata_item_t *gateway_type_item =
		knot_rdata_item(knot_rrset_rdata(rrset), 1);
	if (gateway_type_item == NULL) {
		return NULL;
	}
	/* First two bytes store length. */
	int gateway_type = ((uint8_t *)(gateway_type_item->raw_data))[2];
	switch(gateway_type) {
	case IPSECKEY_NOGATEWAY: {
		char *ret = malloc(sizeof(char) * 4);
		if (ret == NULL) {
			ERR_ALLOC_FAILED;
			return NULL;
		}
		memset(ret, 0, sizeof(char) * 4);
		memcpy(ret, "\".\"", 4);
		return ret;
	}
	case IPSECKEY_IP4:
		return rdata_a_to_string(item);
	case IPSECKEY_IP6:
		return rdata_aaaa_to_string(item);
	case IPSECKEY_DNAME:
		return rdata_binary_dname_to_string(item);
	default:
		return NULL;
	}

	/* Flow *should* not get here. */
	return NULL;
}

char *rdata_nxt_to_string(knot_rdata_item_t item)
{
	char buff[32];
	size_t i;
	uint8_t *bitmap = rdata_item_data(item);
	size_t bitmap_size = rdata_item_size(item);

	char *ret = malloc(sizeof(char) * MAX_NSEC_BIT_STR_LEN);

	memset(ret, 0, MAX_NSEC_BIT_STR_LEN);

	for (i = 0; i < bitmap_size * 8; ++i) {
		if (get_bit(bitmap, i)) {
			if (knot_rrtype_to_string(i, buff, sizeof(buff)) < 0) {
				free(ret);
				return NULL;
			}

			strncat(ret, buff, MAX_RR_TYPE_LEN);
			strncat(ret, " ", 2);
		}
	}

	return ret;
}


char *rdata_nsec_to_string(knot_rdata_item_t item)
{
	char buff[32];

	char *ret = malloc(sizeof(char) * MAX_NSEC_BIT_STR_LEN);
	if (ret == NULL) {
		ERR_ALLOC_FAILED;
		return NULL;
	}
	memset(ret, 0, MAX_NSEC_BIT_STR_LEN);
	uint8_t *data = rdata_item_data(item);

	int increment = 0;
	for (int i = 0; i < rdata_item_size(item); i += increment) {
		increment = 0;
		uint8_t window = data[i];
		increment++;

		uint8_t bitmap_size = data[i + increment];
		increment++;

		uint8_t *bitmap = malloc(sizeof(uint8_t) * bitmap_size);

		memcpy(bitmap, data + i + increment,
		       bitmap_size);

		increment += bitmap_size;

		for (int j = 0; j < bitmap_size * 8; j++) {
			if (get_bit(bitmap, j)) {
				if (knot_rrtype_to_string(j + window * 256,
				                          buff, sizeof(buff))
				    < 0) {
					free(bitmap);
					free(ret);
					return NULL;
				}
				strncat(ret, buff, MAX_RR_TYPE_LEN);
				strncat(ret, " ", 2);
			}
		}

		free(bitmap);
	}

	return ret;
}

char *rdata_unknown_to_string(knot_rdata_item_t item)
{
 	uint16_t size = rdata_item_size(item);
	char *ret =
		malloc(sizeof(char) * (2 * rdata_item_size(item) +
				       strlen("\\# ") + U16_MAX_STR_LEN + 1));
	if (ret == NULL) {
		return NULL;
	}
	memcpy(ret, "\\# \0", strlen("\\# \0"));
	snprintf(ret + strlen("\\# "),
	         strlen("\\# ") + U16_MAX_STR_LEN + 1, "%lu ",
		 (unsigned long) size);
	char *converted = knot_hex_to_string(rdata_item_data(item), size);
	strncat(ret, converted, size * 2 + 1);
	free(converted);
	return ret;
}

char *rdata_loc_to_string(knot_rdata_item_t item)
{
	return rdata_unknown_to_string(item);
}

typedef char * (*item_to_string_t)(knot_rdata_item_t);

static item_to_string_t item_to_string_table[KNOT_RDATA_ZF_UNKNOWN + 1] = {
	rdata_dname_to_string,
	rdata_dns_name_to_string,
	rdata_text_array_to_string,
	rdata_text_array_to_string,
	rdata_byte_to_string,
	rdata_short_to_string,
	rdata_long_to_string,
	rdata_a_to_string,
	rdata_aaaa_to_string,
	rdata_rrtype_to_string,
	rdata_algorithm_to_string,
	rdata_certificate_type_to_string,
	rdata_period_to_string,
	rdata_time_to_string,
	rdata_base64_to_string,
	rdata_base32_to_string,
	rdata_hex_to_string,
	rdata_hexlen_to_string,
	rdata_nsap_to_string,
	rdata_apl_to_string,
	NULL, //rdata_ipsecgateway_to_string,
	rdata_services_to_string,
	rdata_nxt_to_string,
	rdata_nsec_to_string,
	rdata_loc_to_string,
	rdata_unknown_to_string
};

char *rdata_item_to_string(knot_rdata_zoneformat_t type,
                           knot_rdata_item_t item)
{
	return item_to_string_table[type](item);
}

int rdata_dump_text(const knot_rdata_t *rdata, uint16_t type, FILE *f,
                     const knot_rrset_t *rrset)
{
	if (rdata == NULL || rrset == NULL) {
		return KNOT_EINVAL;
	}
	
	knot_rrtype_descriptor_t *desc =
		knot_rrtype_descriptor_by_type(type);
	char *item_str = NULL;
	assert(rdata->count <= desc->length);
	for (int i = 0; i < rdata->count; i++) {
		/* Workaround for IPSec gateway. */
		if (desc->zoneformat[i] == KNOT_RDATA_ZF_IPSECGATEWAY) {
			item_str = rdata_ipsecgateway_to_string(rdata->items[i],
			                                        rrset);
		} else {
			item_str = rdata_item_to_string(desc->zoneformat[i],
						rdata->items[i]);
		}
		if (item_str == NULL) {
			item_str =
				rdata_item_to_string(KNOT_RDATA_ZF_UNKNOWN,
						     rdata->items[i]);
		}

		if (item_str == NULL) {
			/* Fatal error. */
			return KNOT_ERROR;
		}

		if (i != rdata->count - 1) {
			fprintf(f, "%s ", item_str);
		} else {
			fprintf(f, "%s", item_str);
		}

		free(item_str);
	}
	fprintf(f, "\n");

	return KNOT_EOK;
}

#endif

int knot_rrset_txt_dump_header(const knot_rrset_t *rrset, char *dst,
                               const size_t maxlen)
{
	if (rrset == NULL || dst == NULL) {
		return KNOT_EINVAL;
	}

	size_t len = 0;
	char   buf[32];
	int    ret;

	// Dump rrset owner.
	char *name = knot_dname_to_str(rrset->owner);
	ret = snprintf(dst + len, maxlen - len, "%-20s\t", name);
	free(name);
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset ttl.
	if (1) {	
		ret = snprintf(dst + len, maxlen - len, "%6u\t", rrset->ttl);
	} else {
		ret = snprintf(dst + len, maxlen - len, "     \t");
	}
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset class.
	if (1) {
		if (knot_rrclass_to_string(rrset->rclass, buf, sizeof(buf)) < 0)
		{
			return KNOT_ESPACE;
		}
		ret = snprintf(dst + len, maxlen - len, "%-2s\t", buf);
	} else {
		ret = snprintf(dst + len, maxlen - len, "  \t");
	}
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;

	// Dump rrset type.
	if (knot_rrtype_to_string(rrset->type, buf, sizeof(buf)) < 0) {
		return KNOT_ESPACE;
	}
	ret = snprintf(dst + len, maxlen - len, "%-5s\t", buf);
	if (ret < 0 || ret >= maxlen - len) {
		return KNOT_ESPACE;
	}
	len += ret;
	
	return len;
}

int knot_rrset_txt_dump_data(const knot_rrset_t *rrset, const size_t pos,
                             char *dst, const size_t maxlen)
{
	if (rrset == NULL || dst == NULL) {
		return KNOT_EINVAL;
	}

	const uint8_t *rdata = knot_rrset_get_rdata(rrset, pos);
	size_t data_len = rrset_rdata_item_size(rrset, pos);

	switch (knot_rrset_type(rrset)) {
		case KNOT_RRTYPE_A:
			break;
		case KNOT_RRTYPE_NS:
			break;
		case KNOT_RRTYPE_CNAME:
			break;
		case KNOT_RRTYPE_SOA:
			break;
		case KNOT_RRTYPE_PTR:
			break;
		case KNOT_RRTYPE_HINFO:
			break;
		case KNOT_RRTYPE_MINFO:
			break;
		case KNOT_RRTYPE_MX:
			break;
		case KNOT_RRTYPE_TXT:
			break;
		case KNOT_RRTYPE_RP:
			break;
		case KNOT_RRTYPE_AFSDB:
			break;
		case KNOT_RRTYPE_RT:
			break;
		case KNOT_RRTYPE_KEY:
			break;
		case KNOT_RRTYPE_AAAA:
			break;
		case KNOT_RRTYPE_LOC:
			break;
		case KNOT_RRTYPE_SRV:
			break;
		case KNOT_RRTYPE_NAPTR:
			break;
		case KNOT_RRTYPE_KX:
			break;
		case KNOT_RRTYPE_CERT:
			break;
		case KNOT_RRTYPE_DNAME:
			break;
		case KNOT_RRTYPE_APL:
			break;
		case KNOT_RRTYPE_DS:
			break;
		case KNOT_RRTYPE_SSHFP:
			break;
		case KNOT_RRTYPE_IPSECKEY:
			break;
		case KNOT_RRTYPE_RRSIG:
			break;
		case KNOT_RRTYPE_NSEC:
			break;
		case KNOT_RRTYPE_DNSKEY:
			break;
		case KNOT_RRTYPE_DHCID:
			break;
		case KNOT_RRTYPE_NSEC3:
			break;
		case KNOT_RRTYPE_NSEC3PARAM:
			break;
		case KNOT_RRTYPE_TLSA:
			break;
		case KNOT_RRTYPE_SPF:
			break;
		default:
			break;
	}

	return KNOT_EOK;
}

int knot_rrset_txt_dump(const knot_rrset_t *rrset, char *dst, const size_t maxlen)
{
	if (rrset == NULL || dst == NULL) {
		return KNOT_EINVAL;
	}

	size_t len = 0;
	int    ret;

	// Loop over rdata in rrset.
	for (size_t i = 0; i < rrset->rdata_count; i++) {
		// Dump rdata owner, class, ttl and type.
		ret = knot_rrset_txt_dump_header(rrset, dst + len, maxlen - len);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;

		// Dump rdata as such.
		ret = knot_rrset_txt_dump_data(rrset, i, dst + len, maxlen - len);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;
	}

	// Dump RRSIG records if any via recursion call.
	if (rrset->rrsigs != NULL) {
		ret = knot_rrset_txt_dump(rrset->rrsigs, dst + len, maxlen - len);
		if (ret < 0) {
			return KNOT_ESPACE;
		}
		len += ret;
	}

	return len;
}

typedef struct {
	int    ret;
	FILE   *file;
	char   *buf;
	size_t buflen;
	const knot_dname_t *origin;
} dump_params_t;

static void apex_node_dump_text(knot_node_t *node, dump_params_t *params)
{
	knot_rrset_t *rr = knot_node_get_rrset(node, KNOT_RRTYPE_SOA);

	int ret = knot_rrset_txt_dump(rr, params->buf, params->buflen);
	if (ret < 0) {
		params->ret = KNOT_ENOMEM;
		return;
	}
	fprintf(params->file, "%s\n", params->buf);

	const knot_rrset_t **rrsets = knot_node_rrsets(node);
	
	for (int i = 0; i < node->rrset_count; i++) {
		if (rrsets[i]->type != KNOT_RRTYPE_SOA) {
			ret = knot_rrset_txt_dump(rrsets[i], params->buf,
			                          params->buflen);
			if (ret < 0) {
				free(rrsets);
				params->ret = KNOT_ENOMEM;
				return;
			}
			fprintf(params->file, "%s\n", params->buf);
		}
	}

	free(rrsets);

	params->ret = KNOT_EOK;
}

void node_dump_text(knot_node_t *node, void *data)
{
	dump_params_t *params = (dump_params_t *)data;

	if (node->owner == params->origin) {
		apex_node_dump_text(node, params);
		return;
	}

	const knot_rrset_t **rrsets = knot_node_rrsets(node);

	int ret;
	for (int i = 0; i < node->rrset_count; i++) {
		ret = knot_rrset_txt_dump(rrsets[i], params->buf, params->buflen);
		if (ret < 0) {
			params->ret = KNOT_ENOMEM;
			return;
		}
	}

	free(rrsets);

	params->ret = KNOT_EOK;
}

#define DUMP_BUF_LEN (70 * 1024)

int zone_dump_text(knot_zone_contents_t *zone, FILE *file)
{
	if (zone == NULL || file == NULL) {
		return KNOT_EINVAL;
	}

	char *buf = malloc(DUMP_BUF_LEN);
	if (buf == NULL) {
		return KNOT_ENOMEM;
	}

	fprintf(file, ";Dumped using %s v. %s\n", PACKAGE_NAME, PACKAGE_VERSION);

	dump_params_t params;
	params.ret = KNOT_ERROR;
	params.file = file;
	params.buf = buf;
	params.buflen = DUMP_BUF_LEN;
	params.origin = knot_node_owner(knot_zone_contents_apex(zone));

	knot_zone_contents_tree_apply_inorder(zone, node_dump_text, &params);
	if (params.ret != KNOT_EOK) {
		return params.ret;
	}

	knot_zone_contents_nsec3_apply_inorder(zone, node_dump_text, &params);
	if (params.ret != KNOT_EOK) {
		return params.ret;
	}

	return KNOT_EOK;
}
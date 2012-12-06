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

#include "zscanner/test/tests.h"

#include <inttypes.h>			// PRIu64
#include <stdio.h>			// printf
#include <time.h>			// mktime
#include <stdlib.h>			// printf
#include "../scanner_functions.h"	// date_to_timestamp

int test__date_to_timestamp()
{
	time_t    ref_timestamp, max_timestamp;
	uint32_t  test_timestamp;
	uint8_t   buffer[16];
	struct tm tm;

	// Set UTC for strftime.
	putenv("TZ=UTC");
	tzset();

	// Get maximal allowed timestamp.
	strptime("21051231235959", "%Y%m%d%H%M%S", &tm);
	max_timestamp = mktime(&tm);

	// Testing loop over whole input interval.
	for (ref_timestamp = 0;
	     ref_timestamp < max_timestamp;
	     ref_timestamp += 30) {
		// Get reference (correct) timestamp.
		strftime((char*)buffer, sizeof(buffer), "%Y%m%d%H%M%S",
			 gmtime(&ref_timestamp));

		// Get testing timestamp.
		date_to_timestamp(buffer, &test_timestamp);

		// Some continuous loging.
		if (ref_timestamp % 10000000 == 0) {
			printf("%s = %"PRIu64"\n", buffer, ref_timestamp);
		}

		// Comparing results.
		if (ref_timestamp != test_timestamp) {
			if (ref_timestamp > test_timestamp) { 
				printf("%s = %"PRIu64", in - out = %"PRIu64"\n",
				       buffer, ref_timestamp,
				       ref_timestamp - test_timestamp);
			} else {
				printf("%s = %"PRIu64", out - in = %"PRIu64"\n",
				       buffer, ref_timestamp,
				       test_timestamp - ref_timestamp);
			}

			return -1;
		}
	}

	return 0;
}

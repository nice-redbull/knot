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

#include <config.h>
#include <stdbool.h>

#include "utils/common/rr-serialize.h"
#include "knot/zone/zone-dump-text.h"
#include "libknot/libknot.h"

int rdata_write_mem(char* dst, size_t maxlen, const knot_rdata_t *rdata,
                    uint16_t type)
{
	if (dst == NULL || rdata == NULL) {
		return KNOT_EINVAL;
	}
	
	int ret = 0;
	int wb = 0;
	
	knot_rrtype_descriptor_t *desc = NULL;
	desc = knot_rrtype_descriptor_by_type(type);
	char *item_str = NULL;
	assert(rdata->count <= desc->length);

	/* Workaround for IPSec gateway. */
	if (type == KNOT_RRTYPE_IPSECKEY) {
		ret = snprintf(dst+wb, maxlen-wb, "NOT YET IMPLEMENTED");
		if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
		wb += ret;
		return wb;
	}
	
	/*! \todo Implement for TSIG. */
	if (type == KNOT_RRTYPE_TSIG) {
		ret = snprintf(dst+wb, maxlen-wb, "TSIG NOT YET IMPLEMENTED");
		if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
		wb += ret;
		return wb;
	}

	for (int i = 0; i < rdata->count; i++) {
		item_str = rdata_item_to_string(desc->zoneformat[i],
		                                rdata->items[i]);
		if (item_str == NULL) {
			item_str = rdata_item_to_string(KNOT_RDATA_ZF_UNKNOWN,
			                                rdata->items[i]);
		}
		
		if (item_str == NULL) {
			return KNOT_ERROR; 
		}
		
		if (i != rdata->count - 1) {
			ret = snprintf(dst+wb, maxlen-wb, "%s ", item_str);
		} else {
			ret = snprintf(dst+wb, maxlen-wb, "%s", item_str);
		}
		free(item_str);
		if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
		wb += ret;
	}

	return wb;
}

int rrset_header_write_mem(char *dst, size_t maxlen,
                           const knot_rrset_t *rrset,
                           const bool p_class, const bool p_ttl)
{
	if (dst == NULL || rrset == NULL) {
		return KNOT_EINVAL;
	}
	
	char buf[32];
	int  ret = 0;
	int  wb = 0;

	char *name = knot_dname_to_str(rrset->owner);
	ret = snprintf(dst+wb, maxlen-wb, "%-20s\t", name);
	free(name);
	if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
	wb += ret;

	if (p_ttl) {	
		ret = snprintf(dst+wb, maxlen-wb, "%6u\t", rrset->ttl);
	} else {
		ret = snprintf(dst+wb, maxlen-wb, "     \t");
	}
	if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
	wb += ret;
	
	if (p_class) {
		if (knot_rrclass_to_string(rrset->rclass, buf, sizeof(buf)) < 0) {
			return KNOT_ERROR;
		}
		ret = snprintf(dst+wb, maxlen-wb, "%-2s\t", buf);
	} else {
		ret = snprintf(dst+wb, maxlen-wb, "  \t");
	}
	if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
	wb += ret;
	
	if (knot_rrtype_to_string(rrset->type, buf, sizeof(buf)) < 0) {
		return KNOT_ERROR;
	}
	ret = snprintf(dst+wb, maxlen-wb, "%-5s\t",  buf);
	if (ret < 0 || ret >= maxlen-wb) return KNOT_ESPACE;
	wb += ret;
	
	return wb;
}

static int rrsig_write_mem(char *dst, size_t maxlen, knot_rrset_t *rrsig)
{
	if (dst == NULL || rrsig == NULL) {
		return KNOT_EINVAL;
	}
	
	int wb = 0;
	int ret = rrset_header_write_mem(dst, maxlen, rrsig, true, true);
	if (ret < 0) return KNOT_ESPACE;
	wb += ret;
	
	knot_rdata_t *tmp = rrsig->rdata;
	
	while (tmp->next != rrsig->rdata) {
		int ret = rdata_write_mem(dst+wb, maxlen-wb, tmp,
		                          KNOT_RRTYPE_RRSIG);
		if (ret < 0) return ret;
		wb += ret;
		
		ret = rrset_header_write_mem(dst+wb, maxlen-wb, rrsig, true, true);
		tmp = tmp->next;
		if (ret < 0) return ret;
		wb += ret;
	}
	
	ret = rdata_write_mem(dst+wb, maxlen-wb, tmp, KNOT_RRTYPE_RRSIG);
	if (ret < 0) return ret;
	wb += ret;
	
	return KNOT_EOK;
}


int rrset_write_mem(char *dst, size_t maxlen, const knot_rrset_t *rrset)
{
	if (dst == NULL || rrset == NULL) {
		return KNOT_EINVAL;
	}
	
	int ret = 0;
	int wb = 0;

	knot_rdata_t *tmp = rrset->rdata;

	do {
		ret = rrset_header_write_mem(dst+wb, maxlen-wb, rrset, true, true);
		if (ret < 0) return ret;
		wb += ret;

		ret = rdata_write_mem(dst+wb, maxlen-wb, tmp, rrset->type);
		if (ret < 0) return ret;
		wb += ret;

		ret = snprintf(dst+wb, maxlen-wb, "\n");
		if (ret < 0 || ret >= maxlen-wb) return ret;
		wb += ret;

		tmp = tmp->next;
	} while (tmp != rrset->rdata);

	knot_rrset_t *rrsig_set = rrset->rrsigs;
	if (rrsig_set != NULL) {
		ret = rrsig_write_mem(dst+wb, maxlen-wb, rrsig_set);
		if (ret < 0) return ret;
		wb += ret;
	}

	return wb;
}
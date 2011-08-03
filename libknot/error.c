#include "error.h"
#include "utils.h"

#include "common/errors.h"

const error_table_t knot_error_msgs2[KNOT_ERROR_COUNT] = {
	{KNOT_EOK, "OK"},
	{KNOT_ERROR, "General error."},
	{KNOT_ENOMEM, "Not enough memory."},
	{KNOT_EBADARG, "Wrong argument supported."},
	{KNOT_EFEWDATA, "Not enough data to parse."},
	{KNOT_ESPACE, "Not enough space provided."},
	{KNOT_EMALF, "Malformed data."},
	{KNOT_ECRYPTO, "Error in crypto library."},
	{KNOT_ENSEC3PAR, "Missing or wrong NSEC3PARAM record."},
	{KNOT_EBADZONE, "Domain name does not belong to the given zone."},
	{KNOT_EHASH, "Error in hash table."},
	{KNOT_EZONEIN, "Error inserting zone."},
	{KNOT_ENOZONE, "No such zone found."},
	{KNOT_ENONODE, "No such node in zone found."},
	{KNOT_EDNAMEPTR, "Domain name pointer larger than allowed."},
	{KNOT_EPAYLOAD, "Payload in OPT RR larger than max wire size."},
	{KNOT_ECRC, "CRC check failed."},
	{KNOT_ERROR, 0}
};
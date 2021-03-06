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
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knot/conf/includes.h"

/*!
 * \brief Structure to store names of files included into the config.
 */
struct conf_includes {
	int free_index;		//!< First free index in 'names'.
	int capacity;		//!< Maximal capacity.
	char *names[0];		//!< Pointers to store file names.
};

/*!
 * \brief Initialize structure for storing names of included files.
 */
conf_includes_t *conf_includes_init(int capacity)
{
	if (capacity <= 0)
		return NULL;

	size_t size = sizeof(conf_includes_t) + (capacity * sizeof(char *));
	conf_includes_t *result = calloc(1, size);
	if (!result)
		return NULL;

	result->capacity = capacity;
	return result;
}

/*!
 * \brief Free structure for storing the names of included files.
 */
void conf_includes_free(conf_includes_t *includes)
{
	if (!includes)
		return;

	for (int i = 0; i < includes->free_index; i++)
		free(includes->names[i]);

	free(includes);
}

/**
 * \brief Check if there is a capacity to insert new file..
 */
bool conf_includes_can_push(conf_includes_t *includes)
{
	if (!includes)
		return false;

	return includes->free_index < includes->capacity;
}

/**
 * \brief Constructs a path relative to a reference file.
 *
 * e.g. path_relative_to("b.conf", "samples/a.conf") == "samples/b.conf"
 *
 * \param filename   File name of the target file.
 * \param reference  Referece file name (just path is used).
 *
 * \return Relative path to a reference file.
 */
static char *path_relative_to(const char *filename, const char *reference)
{
	char *path_end = strrchr(reference, '/');
	if (!path_end)
		return strdup(filename);

	int path_len = (int)(path_end - reference);
	size_t result_len = path_len + 1 + strlen(filename) + 1;
	char *result = malloc(result_len * sizeof(char));
	if (!result)
		return NULL;

	int w;
	w = snprintf(result, result_len, "%.*s/%s", path_len, reference, filename);
	assert(w + 1 == result_len);

	return result;
}

/**
 * \brief Pushes a file name onto the stack of files.
 */
bool conf_includes_push(conf_includes_t *includes, const char *filename)
{
	if (!includes || !filename)
		return false;

	if (!conf_includes_can_push(includes))
		return false;

	char *store = NULL;

	if (includes->free_index == 0 || filename[0] == '/') {
		store = strdup(filename);
	} else {
		char *previous = includes->names[includes->free_index - 1];
		store = path_relative_to(filename, previous);
	}

	includes->names[includes->free_index++] = store;
	return store != NULL;
}

/**
 * \brief Returns a file name on the top of the stack.
 */
char *conf_includes_top(conf_includes_t *includes)
{
	if (!includes || includes->free_index == 0)
		return NULL;

	return includes->names[includes->free_index - 1];
}

/**
 * \brief Returns a file name on the top of the stack and removes it.
 */
char *conf_includes_pop(conf_includes_t *includes)
{
	char *result = conf_includes_top(includes);
	if (result)
		includes->free_index -= 1;

	return result;
}

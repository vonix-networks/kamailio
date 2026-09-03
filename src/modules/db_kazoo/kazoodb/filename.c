#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdio.h>

struct stat st = {0};

#define KAZOO_DB_DEFAULT_LOCATION "/etc/kazoo/kamailio/db"
#define KAZOO_DB_NAME "kazoo.db"

#define ENV_KAZOO_DB_LOCATION_1 "KAZOO_DB_LOCATION"
#define ENV_KAZOO_DB_LOCATION_2 "KAMAILIO_DB_LOCATION"
#define ENV_KAZOO_DB_NAME "KAZOO_DB_NAME"
#define ENV_KAZOO_DB "KAZOO_DB"

static char kz_filename_buffer[1024];



void ensure_path(const char* Path) {
	if (stat(Path, &st) == -1) {
	    mkdir(Path, 0777);
	}
}

void kazoo_db_filename(const char** ptrToFilename)
{
	char * tmp = NULL;
	const char * location = KAZOO_DB_DEFAULT_LOCATION;
	const char * name = KAZOO_DB_NAME;

	if ((tmp = getenv(ENV_KAZOO_DB_LOCATION_1)) != NULL) {
		location = tmp;
	} else 	if ((tmp = getenv(ENV_KAZOO_DB_LOCATION_2)) != NULL) {
		location = tmp;
	}

	if ((tmp = getenv(ENV_KAZOO_DB_NAME)) != NULL) {
		name = tmp;
	}

	if ((tmp = getenv(ENV_KAZOO_DB)) != NULL) {
		location = dirname(tmp);
		name = basename(tmp);
	}

	ensure_path(location);

	sprintf(kz_filename_buffer, "%s/%s", location, name);
	*ptrToFilename = kz_filename_buffer;
}

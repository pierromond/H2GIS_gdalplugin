#ifndef __H2GIS_GRAALVM_H
#define __H2GIS_GRAALVM_H

#include <graal_isolate.h>


#if defined(__cplusplus)
extern "C" {
#endif

char* h2gis_get_last_error(graal_isolatethread_t*);

long long int h2gis_connect(graal_isolatethread_t*, char*, char*, char*);

long long int h2gis_load(graal_isolatethread_t*, long long int);

long long int h2gis_fetch(graal_isolatethread_t*, long long int, char*);

int h2gis_execute(graal_isolatethread_t*, long long int, char*);

long long int h2gis_prepare(graal_isolatethread_t*, long long int, char*);

void h2gis_bind_double(graal_isolatethread_t*, long long int, int, double);

void h2gis_bind_int(graal_isolatethread_t*, long long int, int, int);

void h2gis_bind_long(graal_isolatethread_t*, long long int, int, long long int);

void h2gis_bind_string(graal_isolatethread_t*, long long int, int, char*);

void h2gis_bind_blob(graal_isolatethread_t*, long long int, int, char*, int);

int h2gis_execute_prepared_update(graal_isolatethread_t*, long long int);

long long int h2gis_execute_prepared(graal_isolatethread_t*, long long int);

void h2gis_close_query(graal_isolatethread_t*, long long int);

void h2gis_close_connection(graal_isolatethread_t*, long long int);

void h2gis_delete_database_and_close(graal_isolatethread_t*, long long int);

void * h2gis_fetch_all(graal_isolatethread_t*, long long int, void *);

void * h2gis_fetch_one(graal_isolatethread_t*, long long int, void *);

void * h2gis_fetch_batch(graal_isolatethread_t*, long long int, int, void *);

void * h2gis_get_column_types(graal_isolatethread_t*, long long int, void *);

char* h2gis_get_metadata_json(graal_isolatethread_t*, long long int);

long long int h2gis_free_result_set(graal_isolatethread_t*, long long int);

void h2gis_free_result_buffer(graal_isolatethread_t*, void *);

#if defined(__cplusplus)
}
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include "bovnar.h"
#include "bvn_io_impl.h"
#include "bvn_val_impl.h"
bool bvn_ser_finish_stream(bvnr_serializer_t *s);
struct bvnr_canon_observer_s {
	bvnr_serializer_t ser;
};
bvnr_canon_observer_t *bvnr_canon_observer_create(
	const bvnr_sink_t *sink, bool pretty)
{
	if (!sink || !bvn_sink_impl_c(sink)->push) return NULL;
	bvnr_canon_observer_t *obs = malloc(sizeof(*obs));
	if (!obs) return NULL;
	memset(obs, 0, sizeof(*obs));
	obs->ser.sink              = *sink;
	obs->ser.pretty            = pretty;
	obs->ser.max_array_nesting = UINT8_MAX;
	return obs;
}
void bvnr_canon_observer_destroy(bvnr_canon_observer_t *obs)
{
	free(obs);
}
bool bvnr_canon_observer_on_event(void *ud,
	bvnr_event_t ev, bvnr_data_t *data)
{
	bvnr_canon_observer_t *obs = (bvnr_canon_observer_t *)ud;
	if (!obs) return true;
	return bvn_ser_serialize_event(&obs->ser, ev, data);
}
bool bvnr_canon_observer_finish(bvnr_canon_observer_t *obs)
{
	if (!obs) return true;
	return bvn_ser_finish_stream(&obs->ser);
}

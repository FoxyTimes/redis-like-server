#include "types.h"

#include "unistd.h"
#include "stdlib.h"

#include <stdlib.h>

void free_resp_object(RespObject *obj) {
    if (!obj) return;

    if (obj->str) {
        free(obj->str);
        obj->str = NULL;
    }

    if (obj->elements > 0 && obj->items) {
        for (int i = 0; i < obj->elements; i++) {
            if (obj->items[i]) {
                free_resp_object(obj->items[i]);
            }
        }
        free(obj->items);
        obj->items = NULL;
    }

    free(obj);
}


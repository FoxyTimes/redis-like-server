#include <string.h>
#include "memory_pool.h"
#include "protocol_parser.h"
#include "logger.h"
#include "database.h"
#include "blocking_queue.h"



static unsigned int hash_str(const char* key) {
    unsigned int h = 0;
    for (size_t i = 0; key[i] != '\0'; ++i) {
        h = h * 31 + (unsigned char)key[i];
    }
    return h;
}


static inline int lock_index_for_key(HashMap* map, const char* key) {
    if ((int)(hash_str(key))==-1) {
        return -1;
    }
    return (int)(hash_str(key) % (unsigned int)map->num_locks);
}

static inline int bucket_index_for_key(HashMap* map, const char* key) {
    return (int)(hash_str(key) % (unsigned int)map->table_size);
}

HashMap* hashmap_create(MemoryPool* mp, int table_size, int num_locks) {
    if (table_size <= 0) return nullptr;

    HashMap* map = xmalloc(sizeof(HashMap));
    if (!map) return nullptr;

    map->table = xmalloc(sizeof(Entry*) * table_size);
    if (!map->table) return nullptr;

    map->order=nullptr;

    memset(map->table, 0, sizeof(Entry*) * table_size);
    map->table_size = table_size;
    map->size = 0;
    map->pool = mp;

    map->num_locks = num_locks;

    map->locks = xmalloc(sizeof(cor_rwlock_t) * map->num_locks);
    if (!map->locks) return nullptr;

    for (int i = 0; i < map->num_locks; ++i) {
        cor_rwlock_init(&map->locks[i]);
    }
    map->total_commands_processed=0;
    map->cmd_get=0;
    map->cmd_del=0;
    map->cmd_set=0;
    cor_rwlock_init(&map->rwlock);

    return map;
}

int hashmap_put(HashMap* map, const char* key, const char* value) {
    size_t key_len = strlen(key)+1;
    size_t value_len = strlen(value)+1;
    LOG(LOG_INFO, "SIZE:  %d", key_len+value_len );
    if (key_len + value_len > map->pool->size) {
        cor_rwlock_unlock(&map->rwlock);
        return 1;
    }

    LOG(LOG_INFO, "key: %s ------- value: %s", key, value);
    if (!map || !key) return 1;
    cor_rwlock_wrlock(&map->rwlock);
    int lidx = lock_index_for_key(map, key);
    int bidx = bucket_index_for_key(map, key);
    cor_rwlock_wrlock(&map->locks[lidx]);

    Entry* e = map->table[bidx];
    while (e) {
        if (strcasecmp(e->key, key) == 0) {
            hashmap_free(map, e);
            break;
        }
        e = e->next;
    }
    Entry* ne = xmalloc(sizeof(Entry));

    ne->key = pool_strdup(map->pool, key);
    if (!ne->key) {
        while (!ne->key) {
            Entry* entry = map->order->end_of_order;
            if (map->order->end_of_order->prev_in_order) {
                map->order->end_of_order=map->order->end_of_order->prev_in_order;
            }
            else {
                map->order=nullptr;
            }
            if(entry==NULL) {
                exit(1);
            }
            if (!hashmap_free(map, entry)) {
                cor_rwlock_unlock(&map->locks[lidx]);
                return 1;
            }
            ne->key = pool_strdup(map->pool, key);
        }
    }
    ne->value = pool_strdup(map->pool, value);
    if (!ne->value) {
        while (!ne->value) {
            Entry* entry = map->order->end_of_order;
            if (map->order->end_of_order->prev_in_order) {
                map->order->end_of_order=map->order->end_of_order->prev_in_order;
            }
            else {
                map->order=nullptr;
            }
            if(entry==NULL) {
                exit(1);
            }
            if (!hashmap_free(map, entry)) {
                cor_rwlock_unlock(&map->locks[lidx]);
                return 1;
            }
            ne->value = pool_strdup(map->pool, value);
        }
    }
    ne->next = map->table[bidx];
    map->table[bidx] = ne;

    atomic_fetch_add(&map->size, 1);
    cor_rwlock_unlock(&map->locks[lidx]);


    if (map->order==NULL) {
        map->order = ne;
        ne->end_of_order=ne;
        ne->next_in_order=nullptr;
        ne->prev_in_order=nullptr;
    }
    else {
        map->order->prev_in_order=ne;
        ne->next_in_order=map->order;
        ne->end_of_order=map->order->end_of_order;
        ne->prev_in_order=nullptr;
        map->order=ne;
    }
    cor_rwlock_unlock(&map->rwlock);
    return 0;
}

int hashmap_del(HashMap *map, const char *key) {
    cor_rwlock_wrlock(&map->rwlock);
    int lidx = lock_index_for_key(map, key);

    int bidx = bucket_index_for_key(map, key);
    cor_rwlock_wrlock(&map->locks[lidx]);

    Entry* e = map->table[bidx];
    while (e) {
        if (strcasecmp(e->key, key) == 0) {
            hashmap_free(map, e);
            cor_rwlock_unlock(&map->locks[lidx]);
            cor_rwlock_unlock(&map->rwlock);
            return 0;
        }
        e = e->next;
    }
    cor_rwlock_unlock(&map->locks[lidx]);
    cor_rwlock_unlock(&map->rwlock);
    return 1;
}


char* hashmap_get(HashMap* map, const char* key) {
    if (!map || !key) {
        return nullptr;
    }

    int lidx = lock_index_for_key(map, key);
    cor_rwlock_rdlock(&map->locks[lidx]);

    int bidx = bucket_index_for_key(map, key);
    Entry* e = map->table[bidx];
    while (e) {
        if (strcasecmp(e->key, key) == 0) {
            char* val = e->value;
            cor_rwlock_unlock(&map->locks[lidx]);
            LOG(LOG_INFO, "key: %s ------- value: %s", key, val);
            return val;
        }
        e = e->next;
    }

    cor_rwlock_unlock(&map->locks[lidx]);
    LOG(LOG_INFO, "NOT_FOUND");
    return nullptr;
}


int hashmap_free(HashMap* map, Entry* e) {
    if (!map || !e) {
        return 0;
    }

    int bidx = bucket_index_for_key(map, e->key);

    Entry* prev = nullptr;
    Entry* cur = map->table[bidx];
    while (cur) {
        if (cur == e) {
            if (prev) prev->next = cur->next;
            else map->table[bidx] = cur->next;

            atomic_fetch_sub(&map->size, 1);

            pool_free(map->pool, cur->key, strlen(cur->key)+1);
            pool_free(map->pool, cur->value, strlen(cur->value)+1);
            if (cur->prev_in_order) {
                cur->prev_in_order->next_in_order=cur->next_in_order;
            }
            if(cur->next_in_order) {
                cur->next_in_order->prev_in_order=cur->prev_in_order;
            }
            if (map->order) {
                if (map->order==cur) {
                    map->order=cur->next_in_order;
                    if (map->order!=NULL) {
                        map->order->prev_in_order=nullptr;
                    }
                }
                else if (map->order->end_of_order==cur) {
                    map->order->end_of_order=cur->prev_in_order;
                }
            }
            free(cur);
            return 1;
        }
        prev = cur;
        cur = cur->next;
    }

    return 0;
}

int hashmap_free_without_struct(HashMap* map, Entry* e) {
    if (!map || !e) return 0;

    int bidx = bucket_index_for_key(map, e->key);

    Entry* prev = nullptr;
    Entry* cur = map->table[bidx];
    while (cur) {
        if (cur == e) {
            if (prev) prev->next = cur->next;
            else map->table[bidx] = cur->next;

            atomic_fetch_sub(&map->size, 1);

            pool_free(map->pool, cur->key, strlen(cur->key)+1);
            pool_free(map->pool, cur->value, strlen(cur->value)+1);
            free(cur);

            return 1;
        }
        prev = cur;
        cur = cur->next;
    }


    return 0;
}

/*void hashmap_destroy(HashMap* map) {
    if (!map) return;

    if (map->locks) {
        for (int i = 0; i < map->num_locks; ++i) {
            cor_rwlock_destroy(&map->locks[i]);
        }
        free(map->locks);
        map->locks = NULL;
    }

    if (map->pool) {
        pool_destroy(map->pool);
    } else {
    }
}*/

void hashmap_dump(HashMap* map) {
    if (!map) return;

    LOG(LOG_INFO, "HashMap dump (size=%d, table_size=%d)", map->size, map->table_size);

    for (int i = 0; i < map->table_size; i++) {
        Entry* entry = map->table[i];
        if (!entry) continue;

        LOG(LOG_INFO, "Bucket %d:", i);
        while (entry) {
            LOG(LOG_INFO, "  Key: '%s', Value: '%s'", entry->key, entry->value);
            entry = entry->next;
        }
    }

    LOG(LOG_INFO, "End of dump");
}


void hashmap_destroy(HashMap* map) {
    if (!map) return;

    // блокировка всей таблицы на запись
    cor_rwlock_wrlock(&map->rwlock);

    // освобождаем таблицу
    if (map->table) {
        for (int i = 0; i < map->table_size; i++) {
            Entry* entry = map->table[i];
            while (entry) {
                Entry* next = entry->next;
                free(entry);
                entry = next;
            }
        }
        free(map->table);
        map->table = nullptr;
    }

    map->order = nullptr;

    if (map->locks) {
        for (int i = 0; i < map->num_locks; i++) {
            cor_rwlock_destroy(&map->locks[i]);
        }
        free(map->locks);
        map->locks = nullptr;
    }

    cor_rwlock_unlock(&map->rwlock);
    cor_rwlock_destroy(&map->rwlock);
    free(map);
}

//-------------------------------------------




Response* create_response(const char* answer, Client* client) {
    if(!client) return nullptr;
    if (!answer) return nullptr;

    Response* resp = (Response*)malloc(sizeof(Response));
    if (!resp) return nullptr;

    resp->answer = strdup(answer);
    if (!resp->answer) {
        free(resp);
        return nullptr;
    }
    resp->client = client;
    return resp;
}

void process_the_command(RespObject* command, HashMap* map) {
    //usleep(100);

    if (command->type != RESP_ARRAY || command->elements < 1) {
        LOG(LOG_ERROR, "-ERR wrong command format");
        command->on_complete(create_response("-ERR wrong command format\r\n", command->client));
        return;
    }

    RespObject *cmd_name_obj = command->items[0];
    if (cmd_name_obj->type != RESP_BULK_STRING) {
        LOG(LOG_ERROR, "-ERR invalid command name");
        command->on_complete(create_response("-ERR invalid command name\r\n", command->client));
        return;
    }

    const char *cmd_name = cmd_name_obj->str;
    if (!cmd_name) {
        LOG(LOG_FATAL, "cmd_name is NULL");
    }
    if (strcasecmp(cmd_name, "SET") == 0) {
        LOG(LOG_INFO, "START SET");
        // SET key value
        if (command->elements != 3) {
            LOG(LOG_ERROR, "-ERR wrong number of arguments for 'SET'");
            command->on_complete(create_response("-ERR wrong number of arguments for 'SET'\r\n", command->client));
            atomic_fetch_add(&map->cmd_set, 1);
            return;
        }

        RespObject *key_obj = command->items[1];
        RespObject *value_obj = command->items[2];

        if (key_obj->type != RESP_BULK_STRING || value_obj->type != RESP_BULK_STRING) {
            LOG(LOG_ERROR, "-ERR invalid arguments for 'SET'");
            command->on_complete(create_response("-ERR invalid arguments for 'SET'\r\n", command->client));
            atomic_fetch_add(&map->cmd_set, 1);
            return;
        }

        int result = hashmap_put(map, key_obj->str, value_obj->str);
        LOG(LOG_INFO, "result is: %d (SET %s = %s)", result, key_obj->str, value_obj->str);
        if (result == 0) {
            command->on_complete(create_response("+OK\r\n", command->client));
        }
        else {
            command->on_complete(create_response("-ERROR\r\n", command->client));
        }
        atomic_fetch_add(&map->cmd_set, 1);

    }
    else if (strcasecmp(cmd_name, "DEL") == 0) {
        // Del key
        if (command->elements != 2) {
            LOG(LOG_ERROR, "-ERR wrong number of arguments for 'DEL'");
            command->on_complete(create_response("-ERR wrong number of arguments for 'DEL'\r\n", command->client));
            atomic_fetch_add(&map->cmd_del, 1);
            return;
        }

        RespObject *key_obj = command->items[1];

        if (key_obj->type != RESP_BULK_STRING) {
            LOG(LOG_ERROR, "-ERR invalid arguments for 'DEL'");
            command->on_complete(create_response("-ERR invalid arguments for 'DEL'\r\n", command->client));
            atomic_fetch_add(&map->cmd_del, 1);
            return;
        }

        int result = hashmap_del(map, key_obj->str);
        LOG(LOG_INFO, "result is: %d (DEL %s)", result, key_obj->str);
        if (result==0) {
            command->on_complete(create_response("+OK\r\n", command->client));
            atomic_fetch_add(&map->cmd_del, 1);
        }
        else {
            command->on_complete(create_response("+Error with del\r\n", command->client));
            atomic_fetch_add(&map->cmd_del, 1);
        }

    }
    else if (strcasecmp(cmd_name, "GET") == 0) {
        // GET key
        if (command->elements != 2) {
            LOG(LOG_ERROR, "-ERR wrong number of arguments for 'GET'");
            command->on_complete(create_response("-ERR wrong number of arguments for 'GET'\r\n", command->client));
            atomic_fetch_add(&map->cmd_get, 1);
            return;
        }

        RespObject *key_obj = command->items[1];
        if (key_obj->type != RESP_BULK_STRING) {
            LOG(LOG_ERROR, "-ERR invalid argument for 'GET'");
            command->on_complete(create_response("-ERR invalid argument for 'GET'\r\n", command->client));
            atomic_fetch_add(&map->cmd_get, 1);
            return;
        }

        char *value = hashmap_get(map, key_obj->str);
        if (value) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer), "$%zu\r\n%s\r\n", strlen(value), value);
            LOG(LOG_INFO, "GET %s -> %s", key_obj->str, value);
            command->on_complete(create_response(buffer, command->client));
            atomic_fetch_add(&map->cmd_get, 1);
        } else {
            LOG(LOG_INFO, "GET %s -> (nil)", key_obj->str);
            command->on_complete(create_response("$-1\r\n", command->client));
            atomic_fetch_add(&map->cmd_get, 1);
        }

    } else if (strcasecmp(cmd_name, "STATS") == 0) {
        // STATS key
        if (command->elements != 1) {
            LOG(LOG_ERROR, "-ERR wrong number of arguments for 'STATS'");
            command->on_complete(create_response("-ERR wrong number of arguments for 'STATS'\r\n", command->client));
            return;
        }

        char payload[1024];

        int total = atomic_load(&map->total_commands_processed);
        int get   = atomic_load(&map->cmd_get);
        int set   = atomic_load(&map->cmd_set);
        int del   = atomic_load(&map->cmd_del);

        int len = snprintf(payload, sizeof(payload),
                           " total_commands_processed: %d"
                           " cmd_get: %d"
                           " cmd_set: %d"
                           " cmd_del: %d"
                           " free_memory: %zu",
                           total, get, set, del, count_free_space(map->pool));

        char buffer[1050];
        snprintf(buffer, sizeof(buffer), "$%d\r\n%s\r\n", len, payload);


        command->on_complete(create_response(buffer, command->client));
    }
    else if (strcasecmp(cmd_name, "COMMAND") == 0) {
        // Отправляем пустой массив на подключение
        LOG(LOG_DEBUG, "COMMAND called -> empty array");
        command->on_complete(create_response("*0\r\n", command->client));

    } else {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "-ERR unknown command '%s'\r\n", cmd_name);
        LOG(LOG_ERROR, "-ERR unknown command '%s'", cmd_name);
        command->on_complete(create_response(buffer, command->client));
    }
    atomic_fetch_add(&map->total_commands_processed, 1);

}




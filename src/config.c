#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "stdbool.h"
#include "stdio.h"
#include "include/config.h"
#include "include/logger.h"


bool is_config_init = false; // создан ли уже конфиг
char* key_symbols = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"; //допустимые символы для ключа
char commentSymbol = '#'; //символ комментария

#define HASH_TABLE_SIZE 256

struct ConfigVariableEntry *table[HASH_TABLE_SIZE] = {0};

unsigned int hash(const char *key) {
    unsigned int hash = 0;
    while (*key) {
        hash = (hash * 31) + (unsigned char)(*key++);
    }
    return hash % HASH_TABLE_SIZE;
}

int hash_table_insert(const char *key, struct ConfigVariable variable) {
    unsigned int index = hash(key);
    struct ConfigVariableEntry *current = table[index];

    while (current) {
        if (strcmp(current->key, key) == 0) {
            fprintf(stderr, "Key '%s' already exists.\n", key);
            return 1;
        }
        current = current->next;
    }

    struct ConfigVariableEntry *entry = malloc(sizeof(struct ConfigVariableEntry));
    if (!entry) return 1;

    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        return 1;
    }

    entry->variable = variable;
    entry->next = table[index];
    table[index] = entry;

    return 0;
}

struct ConfigVariable *hash_table_get(const char *key) {
    unsigned int index = hash(key);
    struct ConfigVariableEntry *current = table[index];

    while (current) {
        if (strcmp(current->key, key) == 0) {
            return &current->variable;
        }
        current = current->next;
    }
    return NULL;
}

void hash_table_free() {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        struct ConfigVariableEntry *current = table[i];
        while (current) {
            struct ConfigVariableEntry *next = current->next;
            free(current->key);
            // Освобождение данных зависит от типа
            if (current->variable.type == INTEGER) {
                free(current->variable.data.integer);
            } else if (current->variable.type == REAL) {
                free(current->variable.data.real);
            } else if (current->variable.type == STRING) {
                for (int j = 0; current->variable.data.string[j] != NULL; j++) {
                    free(current->variable.data.string[j]);
                }
                free(current->variable.data.string);
            }
            free(current);
            current = next;
        }
        table[i] = NULL;
    }
}

void print_hash_table() {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        struct ConfigVariableEntry *current = table[i];
        while (current) {
            printf("Key: %s\n", current->key);
            print_config_data(current->variable.data, current->variable.type);
            current = current->next;
        }
    }
}

int create_config_table(void) {
    if (is_config_init) {
        LOG(LOG_ERROR, "Config is already initialized");
        return 1;

    }
    is_config_init = true;
    return 0;
}

int destroy_config_table(void) {
    if(!is_config_init) {
        LOG(LOG_ERROR, "Config is not initialized");
        return 1;
    }
    is_config_init = false;
    hash_table_free();
    return 0;
}

int parse_config(const char* path) {
    if (path==NULL) {
        printf("Path to config is NULL\n");
        return 1;
    }

    FILE* file = fopen(path, "r");
    if (file == NULL) {
        LOG(LOG_ERROR, "File %s not found", path);
        return 1;
    }
    LOG(LOG_INFO, "Start read config file %s", path);
    // парсим файл
    int number_string=0;
    for (char line[256]; fgets(line, sizeof(line), file) != NULL;) {
        number_string++;
        // удаляем пробелы в начале и конце строки
        trim(line);

        if (!line[0]) {
            continue;
        }

        char **key_value = split_string_for_key_value(line);
        if (key_value == NULL) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            continue;
        }

        if (key_value[0] == NULL || key_value[1] == NULL) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            free(key_value[0]);
            free(key_value[1]);
            free(key_value);
            continue;
        }

        // проверяем корректность ключа
        if (check_key_for_correctness(key_value[0]) != 0) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            free(key_value[0]);
            free(key_value[1]);
            free(key_value);
            continue;
        }
        // парсим значение
        char **massive = parse_massive(key_value[1]);
        if (massive == NULL) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            free(key_value[0]);
            free(key_value[1]);
            free(key_value);
            continue;
        }
        // создаём конфигурационную переменную
        trim_massive(massive);
        enum ConfigVarType type = parse_type(massive[0]);
        union ConfigData data = create_config_data(massive, type);
        if (data.integer == NULL && data.real == NULL && data.string == NULL) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            free(key_value[0]);
            free(key_value[1]);
            free(key_value);
            continue;
        }
        struct ConfigVariable var = {0};
        var.data = data;
        var.type = type;
        var.name = key_value[0];
        if (define_variable(var)) {
            LOG(LOG_ERROR, "Configuration file %s has syntax error at %d: %s", path, number_string, line);
            free(key_value[0]);
            free(key_value[1]);
            free(key_value);
            continue;
        }

        // освобождаем строки
        for (int i = 0; massive[i] != NULL; i++) {
            free(massive[i]);
        }
        free(massive);

        free(key_value[0]);
        free(key_value[1]);
        free(key_value);
    }
    fclose(file);
    return 0;
}

char ** split_string_for_key_value(const char *str) {
    if (str == NULL) {
        return NULL;
    }

    // Выделяем память для результата (ключ и значение)
    char** result = calloc(sizeof(char*), 2);
    if (result == NULL) {
        LOG(LOG_ERROR, "Memory allocation failed. Split is failed.");
        return NULL;
    }

    // Создаём временную копию строки, чтобы работать с ней
    char *temp = strdup(str);
    if (temp == NULL) {
        free(result);
        LOG(LOG_ERROR, "Memory allocation failed. Split is failed.");
        return NULL;
    }

    char *ptr = temp;
    int in_quotes = 0;  // флаг, показывающий, находимся ли мы внутри кавычек
    char quote_char = 0;  // символ кавычек
    int count = 0;

    char *start = ptr;
    while (*ptr) {
        if (in_quotes) {
            if (*ptr == quote_char) {
                in_quotes = 0;
            }
        } else {
            if (*ptr == '"') {
                in_quotes = 1;
                quote_char = '"';
            }
            else if (*ptr == commentSymbol) {
                *ptr = '\0';
                break;  // игнорируем все, что после комментария
            }
            else if (*ptr == '=') {
                *ptr = '\0';
                result[count] = strdup(start);  // сохраняем ключ
                if (result[count] == NULL) {
                    LOG(LOG_ERROR, "Memory allocation failed. Split is failed.");
                    free(temp);
                    free(result);
                    return NULL;
                }
                count++;
                start = ptr + 1;
            }
        }
        ptr++;
    }

    if (count == 1) {
        result[count] = strdup(start);
        if (result[count] == NULL) {
            LOG(LOG_ERROR, "Memory allocation failed. Split is failed.");
            free(temp);
            free(result);
            return NULL;
        }
    }

    // Проверяем, что мы разобрали строку на два компонента
    if (count != 1 || result[1] == NULL) {
        free(temp);
        for (int i = 0; i < 2; i++) {
            if (result[i]) free(result[i]);
        }
        free(result);
        return NULL;
    }

    // Освобождаем временную копию строки
    free(temp);
    LOG(LOG_DEBUG, "String has splited. Key: %s, Value: %s", result[0], result[1]);

    // Возвращаем пару ключ-значение
    return result;
}


int check_key_for_correctness(char * key) {
    if (key == NULL) {
        LOG(LOG_ERROR, "Key is NULL. Key is bad.");
        return 1;
    }
    for (size_t i=0; i<strlen(key); i++) {
        if (strchr(key_symbols, key[i]) == NULL) {
            return 1;
        }
    }
    return 0;
}

char ** parse_massive(char * value) {
    if (value==NULL) {
        return NULL;
    }

    char *copy = strdup(value);
    if (!copy) {
        LOG(LOG_ERROR, "Memory allocation failed.");
        return NULL;
    }
    trim(copy); //удаляем пробелы слева и справа

    char **result = NULL;
    int count = 0;

    if (copy[0] == '[') {
        // Найдём конец массива с учётом кавычек
        char *ptr = copy + 1;
        int in_quotes = 0;
        char quote_char = 0;
        char *array_end = NULL;

        while (*ptr) {
            if (in_quotes) {
                if (*ptr == quote_char) {
                    in_quotes = 0;
                }
            } else {
                if (*ptr == '"') {
                    in_quotes = 1;
                    quote_char = *ptr;
                } else if (*ptr == ']') {
                    array_end = ptr;
                    break;
                }
            }
            ptr++;
        }

        if (!array_end) {
            free(copy);
            return NULL;
        }

        *array_end = '\0';
        char *inside = copy + 1;

        // Разбор элементов
        ptr = inside;
        char *start = ptr;
        in_quotes = 0;
        quote_char = 0;

        while (*ptr) {
            if (in_quotes) {
                if (*ptr == quote_char) {
                    in_quotes = 0;
                }
            } else {
                if (*ptr == '"') {
                    in_quotes = 1;
                    quote_char = *ptr;
                }
                else if (*ptr == ',') {
                    *ptr = '\0';
                    if (start!=ptr) {
                        result = realloc(result, sizeof(char *) * (count + 2));
                        result[count++] = strdup(start);
                        if (result[count - 1] == NULL) {
                            LOG(LOG_ERROR, "Memory allocation failed.");
                            free(copy);
                            return NULL;
                        }
                    }
                    start = ptr + 1;
                }
            }
            ptr++;
        }

        if (*start) {
            result = realloc(result, sizeof(char *) * (count + 2));
            result[count++] = strdup(start);
        }
        free(copy);
        if (result) result[count] = NULL;
        return result;
    } else {
        // Одиночное значение
        trim(copy);
        if (*copy) {
            result = malloc(sizeof(char *) * 2);
            result[0] = strdup(copy);
            if (result[0] == NULL) {
                LOG(LOG_ERROR, "Memory allocation failed.");
                free(copy);
                return NULL;
            }
            result[1] = NULL;
            free(copy);
            return result;
        }
    }
}

void trim_massive(char ** massive) {
    for (int i=0; massive[i] != NULL; i++) {
        trim(massive[i]);
    }
}

enum ConfigVarType parse_type(char * string) {
    if (string == NULL) {
        return UNDEFINED;
    }
    char * endptr;
    strtol(string, &endptr, 10);
    if (*endptr == '\0') {
        return INTEGER;
    }
    strtod(string, &endptr);
    if (*endptr == '\0') {
        return REAL;
    }
    //если в кавычках, то строка
    if (string[0] == '"' && string[strlen(string)-1] == '"' && strlen(string)>=2) {
        return STRING;
    }
    return UNDEFINED;
}

void trim(char * string) {
    if (string == NULL) {
        return;
    }
    char * start = string;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    char * end = string + strlen(string) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    memmove(string, start, end - start + 2);
}

union ConfigData create_config_data(char ** massive, enum ConfigVarType type) {
    union ConfigData data = {NULL};

    if (massive==NULL) {
        return (union ConfigData){NULL};
    }

    int count = 0;
    for (int i=0; massive[i] != NULL; i++) {
        count++;
    }

    if (type == INTEGER) {
        data.integer = calloc(sizeof(long long int), count+1);
        if (data.integer == NULL) {
            LOG(LOG_ERROR, "Memory allocation failed. Value is bad.");
            return (union ConfigData){NULL};
        }
    }
    else if (type == REAL) {
        data.real = calloc(sizeof(long double), count+1);
        if (data.real == NULL) {
            LOG(LOG_ERROR, "Memory allocation failed. Value is bad.");
            return (union ConfigData){NULL};
        }
    }
    else if (type == STRING) {
        data.string = calloc(sizeof(char*), count+1);
        if (data.string == NULL) {
            LOG(LOG_ERROR, "Memory allocation failed. Value is bad.");
            return (union ConfigData){NULL};
        }
    }
    else {
        return (union ConfigData){NULL};
    }

    for (int i=0; massive[i] != NULL; i++) {
        if (parse_type(massive[i])!=type) {
            if (type == INTEGER) {
                free(data.integer);
                return (union ConfigData){NULL};
            }
            else if (type == REAL) {
                free(data.real);
                return (union ConfigData){NULL};
            }
            else {
                for (int j = 0; j < i; j++) {
                    free(data.string[j]);
                }
                return (union ConfigData){NULL};
            }
        }
        if (type == INTEGER) {
            data.integer[0]++;
            data.integer[i+1] = strtol(massive[i], NULL, 10);
        }
        else if (type == REAL) {
            data.real[0]++;
            data.real[i+1] = strtod(massive[i], NULL);
        }
        else {
            data.string[i] = strdup(massive[i]);
            if (data.string[i] == NULL) {
                for (int j = 0; j < i; j++) {
                    free(data.string[j]);
                }
                free(data.string);
                LOG(LOG_ERROR, "Memory allocation failed. Value is bad.");
                return (union ConfigData){NULL};
            }
            size_t len = strlen(data.string[i]);
            if (len > 1) {
                memmove(data.string[i], data.string[i] + 1, len - 2);
                data.string[i][len - 2] = '\0';
            }
        }
    }
    return data;
}

int define_variable(const struct ConfigVariable variable) {
    if (variable.name == NULL) {
        return 1;
    }
    if (variable.type == UNDEFINED) {
        return 1;
    }
    if (variable.type==STRING) {
        if (variable.data.string == NULL) {
            return 1;
        }
    }
    else if (variable.type==INTEGER) {
        if (variable.data.integer == NULL) {
            return 1;
        }
    }
    else if (variable.type==REAL) {
        if (variable.data.real == NULL) {
            return 1;
        }
    }
    if (hash_table_insert(variable.name, variable)) {
        LOG(LOG_ERROR, "Failed to insert variable into hash table. Variable is bad.");
        return 1;
    }
    return 0;
}

struct ConfigVariable get_variable(const char * name) {
    if (name == NULL) {
        LOG(LOG_ERROR, "Variable name is NULL. Variable is bad.");
        struct ConfigVariable undef;
        undef.type = UNDEFINED;
        return undef;
    }
    struct ConfigVariable *var = hash_table_get(name);
    if (var == NULL) {
        LOG(LOG_ERROR, "Variable '%s' not found.", name);
        struct ConfigVariable undef;
        undef.type = UNDEFINED;
        return undef;
    }
    return *var;
}

int set_variable(const struct ConfigVariable variable) {
    if (variable.name == NULL) {
        LOG(LOG_ERROR, "Variable name is NULL. Variable is bad.");
        return 1;
    }
    if (variable.type == UNDEFINED) {
        LOG(LOG_ERROR, "Variable type is UNDEFINED. Variable is bad.");
        return 1;
    }
    if (variable.type==STRING) {
        if (variable.data.string == NULL) {
            LOG(LOG_ERROR, "Variable data is NULL. Variable is bad.");
            return 1;
        }
    }
    else if (variable.type==INTEGER) {
        if (variable.data.integer == NULL) {
            LOG(LOG_ERROR, "Variable data is NULL. Variable is bad.");
            return 1;
        }
    }
    else if (variable.type==REAL) {
        if (variable.data.real == NULL) {
            LOG(LOG_ERROR, "Variable data is NULL. Variable is bad.");
            return 1;
        }
    }
    struct ConfigVariable *var = hash_table_get(variable.name);
    if (var == NULL) {
        define_variable(variable);
    }
    var = hash_table_get(variable.name);
    var->data = variable.data;
    var->type = variable.type;
    var->description = variable.description;
    return 0;
}

int does_variable_exist(const char * name) {
    if (name == NULL) {
        LOG(LOG_ERROR, "Variable name is NULL. Variable is bad.");
        return false;
    }
    struct ConfigVariable *var = hash_table_get(name);
    if (var == NULL) {
        return false;
    }
    return true;
}

void print_config_data(union ConfigData data, enum ConfigVarType type) {
    if (type == INTEGER) {
        if (data.integer==NULL) {
            printf("Integer values: NULL\n");
            return;
        }
        printf("Integer values: ");
        for (int i=1; i<data.integer[0]+1; i++) {
            printf("%lld ", data.integer[i]);
        }
        printf("\n");
    }
    else if (type == REAL) {
        if (data.real==NULL) {
            printf("Real values: NULL\n");
            return;
        }
        printf("Real values: ");
        for (int i=1; i<data.real[0]+1; i++) {
            printf("%lf ", data.real[i]);
        }
        printf("\n");
    }
    else if (type == STRING) {
        if (data.string==NULL) {
            printf("String values: NULL\n");
            return;
        }
        printf("String values: ");
        for (int i=0; data.string[i] != NULL; i++) {
            printf("%s ", data.string[i]);
        }
        printf("\n");
    }
}
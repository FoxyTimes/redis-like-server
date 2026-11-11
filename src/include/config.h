#pragma once

#include <stdint-gcc.h>

union ConfigData {
    int64_t* integer;
    double* real;
    char** string;
};

enum ConfigVarType {
    UNDEFINED = 0,
    INTEGER = 1,
    REAL,
    STRING,
};

struct ConfigVariable {
    char* name;
    char* description;
    union ConfigData data;
    enum ConfigVarType type;
};

struct ConfigVariableEntry {
    char *key;
    struct ConfigVariable variable;
    struct ConfigVariableEntry *next;
};


unsigned int hash(const char *key);
int hash_table_insert(const char *key, struct ConfigVariable variable);
struct ConfigVariable *hash_table_get(const char *key);
void hash_table_free();
void print_hash_table();
int create_config_table(void);
void set_path(const char* new_path);
int destroy_config_table(void);
int parse_config(const char* path);
char ** split_string_for_key_value(const char *str);
int check_key_for_correctness(char * key);
char ** parse_massive(char * value);
enum ConfigVarType parse_type(char * string);
void trim(char * string);
void trim_massive(char ** massive);
union ConfigData create_config_data(char ** massive, enum ConfigVarType type);
int define_variable(const struct ConfigVariable variable);
struct ConfigVariable get_variable(const char * name);
int set_variable(const struct ConfigVariable variable);
int does_variable_exist(const char * name);
void print_config_data(union ConfigData data, enum ConfigVarType type);
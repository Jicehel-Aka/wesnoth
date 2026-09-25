#ifndef CJSON_TEST_STUB_H
#define CJSON_TEST_STUB_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;

#define cJSON_False  0
#define cJSON_True   1
#define cJSON_NULL   2
#define cJSON_Number 3
#define cJSON_String 4
#define cJSON_Array  5
#define cJSON_Object 6
#define cJSON_Raw    7
#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

cJSON *cJSON_Parse(const char *value);
cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length);
const char *cJSON_GetErrorPtr(void);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
int cJSON_IsString(const cJSON * const item);
int cJSON_IsNumber(const cJSON * const item);
int cJSON_IsObject(const cJSON * const item);
int cJSON_IsArray(const cJSON * const item);
int cJSON_IsBool(const cJSON * const item);
int cJSON_IsTrue(const cJSON * const item);
void cJSON_Delete(cJSON *item);

#define cJSON_ArrayForEach(element, array) \
    for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)

#ifdef __cplusplus
}
#endif
#endif

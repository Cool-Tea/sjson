#ifndef SJSON_H

/* ======== META DATA ======== */

#define SJSON_VERSION "1.0.0"

/* ======== MACROS ======== */

#define jcast(value, type) ((type)(value))
#define jas_node(node) jcast((node), jnode_t*)
#define jas_null(node) jcast((node), jnull_t*)
#define jas_bool(node) jcast((node), jbool_t*)
#define jas_number(node) jcast((node), jnumber_t*)
#define jas_string(node) jcast((node), jstring_t*)
#define jas_array(node) jcast((node), jarray_t*)
#define jas_object(node) jcast((node), jobject_t*)

#define jis_null(node) ((node)->type == JNULL)
#define jis_boolean(node) ((node)->type == JBOOLEAN)
#define jis_number(node) ((node)->type == JNUMBER)
#define jis_string(node) ((node)->type == JSTRING)
#define jis_array(node) ((node)->type == JARRAY)
#define jis_object(node) ((node)->type == JOBJECT)

#define jvector(type, name) \
  struct {                  \
    int len;                \
    int capacity;           \
    type* data;             \
  } name

#define jvector_len(v) ((v).len)
#define jvector_capacity(v) ((v).capacity)
#define jvector_data(v) ((v).data)
#define jvector_get(v, index) ((v).data + index)
#define jvector_foreach(i, v) for (int i = 0; i < jvector_len(v); i++)

/* ======== STRUCTS ======== */

typedef enum jtype {
  JNULL = 0,
  JBOOLEAN,
  JNUMBER,
  JSTRING,
  JARRAY,
  JOBJECT,
} jtype_t;

typedef struct jnode {
  jtype_t type;
} jnode_t;

typedef struct jnull {
  jtype_t type;
} jnull_t;

typedef struct jbool {
  jtype_t type;
  int value;
} jbool_t;

typedef struct jnumber {
  jtype_t type;
  double value;
} jnumber_t;

typedef struct jstring {
  jtype_t type;
  jvector(char, string);
} jstring_t;

typedef struct jarray {
  jtype_t type;
  jvector(jnode_t*, array);
} jarray_t;

/* key-value */
typedef struct jkv {
  char* key;
  jnode_t* value;
  struct jkv* next;  // linked list with dummy head
} jkv_t;

typedef struct jobject {
  jtype_t type;
  jvector(jkv_t, hashmap);
} jobject_t;

/* ======== FUNCTIONS ======== */

char* jto_string(jnode_t* jnode);  // returned string should be freed manually
jnode_t* jfrom_string(const char* json_str);

jnode_t* jnull_new();           // return a singleton pointer
jnode_t* jbool_new(int value);  // return a singleton pointer
jnode_t* jnumber_new(double value);
jnode_t* jstring_new(
    int len, const char* string);  // when len is 0, automatically call strlen
jnode_t* jarray_new();
jnode_t* jobject_new();
void jdelete(jnode_t* jnode);

int jarray_size(jnode_t* jnode);
jnode_t* jarray_get(jnode_t* jnode, int index);
int jarray_add(jnode_t* jnode, jnode_t* value);  // move into array
int jarray_insert(jnode_t* jnode, int index,
                  jnode_t* value);  // move into array
int jarray_pop(jnode_t* jnode);
int jarray_remove(jnode_t* jnode, int index);

int jobject_size(jnode_t* jnode);
int jobject_has(jnode_t* jnode, const char* key);
jnode_t* jobject_get(jnode_t* jnode, const char* key);
int jobject_put(jnode_t* jnode, const char* key,
                jnode_t* value);  // move when non-exists. overwrite when
                                  // exists. erase when value is null.

#endif
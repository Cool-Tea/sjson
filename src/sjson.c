#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sjson.h"

/* ==========================
 *       ERROR OPERATION
 * ========================== */

#define MSG_BUFFER_LEN 1024
#define jerror_clear() \
  do {                 \
    has_err = 0;       \
    err_msg[0] = 0;    \
  } while (0)
#define jenter()                   \
  do {                             \
    if (!st_depth) jerror_clear(); \
    st_depth++;                    \
  } while (0)
#define jleave(ret_val) \
  do {                  \
    st_depth--;         \
    return ret_val;     \
  } while (0)
#define jerror_log(fmt, ...)                \
  do {                                      \
    has_err = 1;                            \
    sprintf(err_msg, (fmt), ##__VA_ARGS__); \
  } while (0)

static int st_depth = 0;
static int has_err = 0;
static char err_msg[MSG_BUFFER_LEN];

const char* jerror() { return has_err ? err_msg : 0; }

/* =======================
 *          UTILS
 * ======================= */

#define check_type(node, type, ret, ...)                      \
  do {                                                        \
    if (!jis_##type(node)) {                                  \
      jerror_log("Expect type '%s' but got type '%s'", #type, \
                 type_str[jtype(node)]);                      \
      ##__VA_ARGS__ jleave(ret);                              \
    }                                                         \
  } while (0)
#define grow_capacity(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

static const char* type_str[] = {
    [JNULL] = "null",     [JBOOLEAN] = "boolean", [JNUMBER] = "number",
    [JSTRING] = "string", [JARRAY] = "array",     [JOBJECT] = "object",
};

static void* reallocate(void* ptr, int old, int new) {
  if (!new) {
    free(ptr);
    return 0;
  } else if (!old) {
    if (!(ptr = malloc(new))) {
      jerror_log("Insufficient memory.");
      return 0;
    }
    return ptr;
  } else {
    if (!(ptr = realloc(ptr, new))) {
      jerror_log("Insufficient memory.");
      return 0;
    }
    return ptr;
  }
}

/* Main hash function for hash table */
static unsigned int fnv1a(const char* str) {
  unsigned int hash = 2166136261u;
  while (*str) {
    hash ^= (unsigned char)*str++;
    hash *= 16777619;
  }
  return hash;
}

/* ==========================
 *      VECTOR OPERATION
 * ========================== */

#define jas_tv(v) jcast((v), tv*)
#define jvector_init(type, v) tvector_init(jas_tv((v)))
#define jvector_free(type, v) tvector_free(jas_tv((v)))
#define jvector_concat(type, v, value, len) \
  tvector_add(jas_tv((v)), (value), (len), sizeof(type))
#define jvector_insert(type, v, index, value, len) \
  tvector_insert(jas_tv((v)), (index), (value), (len), sizeof(type))
#define jvector_pop(type, v, len) \
  jcast(tvector_pop(jcast((v), tv*), (len), sizeof(type)), type*)
#define jvector_remove(type, v, index, len) \
  jcast(tvector_remove(jcast((v), tv*), (index), (len), sizeof(type)), type*)

/* Template */
typedef struct tvector {
  int len;
  int capacity;
  void* data;
} tv;

static void tvector_init(tv* v) {
  v->len = v->capacity = 0;
  v->data = 0;
}

static void tvector_free(tv* v) { reallocate(v->data, 0, 0); }

static int tvector_add(tv* v, const void* value, int len, int typesz) {
  if (v->len + len > v->capacity) {
    int old = v->capacity * typesz;
    while (v->len + len > v->capacity) v->capacity = grow_capacity(v->capacity);
    int new = v->capacity * typesz;
    v->data = reallocate(v->data, old, new);
    if (!v->data) return 0;
  }

  void* target = v->data + v->len * typesz;
  memcpy(target, value, typesz * len);
  v->len += len;
  return 1;
}

/* Popped value will remain at the end of the vector until next write.
 * User should maintain those contents. */
static void* tvector_pop(tv* v, int len, int typesz) {
  v->len -= len;
  if (v->len < 0) v->len = 0;
  return v->data + v->len * typesz;
}

static int tvector_right_shift(tv* v, int index, int distance, int typesz) {
  if (v->len == 0) return 1;

  // Ensure there is enough memory
  const void* last_item = v->data + (v->len - 1) * typesz;
  if (!tvector_add(v, last_item, distance, typesz)) return 0;

  // Shifting
  int len = v->len - index;
  const void* start = v->data + index * typesz;
  void* target = v->data + (index + distance) * typesz;
  memmove(target, start, len * typesz);
  return 1;
}

/* Overwritten contents will be swapped to the end of the vector until next
 * write. User should maintain those contents. */
static void* tvector_left_shift(tv* v, int index, int distance, int typesz) {
  if (index < distance) distance = index;

  // Byte2Byte Swapping
  int len = v->len - index;
  unsigned char* start = v->data + index * typesz;
  unsigned char* target = v->data + (index - distance) * typesz;
  for (int i = 0; i < len * typesz; i++) {
    unsigned char t = target[i];
    target[i] = start[i];
    start[i] = t;
  }

  v->len -= distance;
  return v->data + v->len * typesz;
}

static int tvector_insert(tv* v, int index, const void* value, int len,
                          int typesz) {
  if (index < 0 || index >= v->len) {
    jerror_log("Invalid index '%d'.", index);
    return 0;
  }
  if (!tvector_right_shift(v, index, len, typesz)) return 0;
  void* start = v->data + index * typesz;
  memcpy(start, value, len * typesz);
  return 1;
}

/* Removed value will remain at the end of the vector until next write.
 * User should maintain those contents. */
static void* tvector_remove(tv* v, int index, int len, int typesz) {
  if (index < 0 || index >= v->len) {
    jerror_log("Invalid index '%d'.", index);
    return 0;
  }
  return tvector_left_shift(v, index + len, len, typesz);
}

/* ==============================
 *      HASH TABLE OPERATION
 * ============================== */

#define jht_size(ht) jvector_len(ht)
#define jht_capacity(ht) jvector_capacity(ht)
#define jht_data(ht) jvector_data(ht)
#define jht_get(ht, index) jvector_get((ht), (index))
#define jht_max_len 4
#define jht_capacity_grow(capacity) grow_capacity(capacity)
#define jht_index(ht, key) (fnv1a(key) % jht_capacity(ht))
#define jht_head(ht, key) jvector_get((ht), jht_index((ht), key))

static int jht_init(tv* ht) {
  jvector_init(jkv_t, ht);
  ht->capacity = jht_capacity_grow(ht->capacity);
  int new = ht->capacity * sizeof(jkv_t);
  ht->data = reallocate(0, 0, new);
  if (!ht->data) return 0;
  memset(ht->data, 0, new);
  return 1;
}

static void jht_free(tv* ht) {
  for (int i = 0; i < ht->capacity; i++) {
    jkv_t* head = ht->data + i * sizeof(jkv_t);
    while (head->next) {
      jkv_t* entry = head->next;
      head->next = entry->next;

      jdelete(entry->value);
      reallocate(entry->key, 0, 0);
      reallocate(entry, sizeof(jkv_t), 0);
    }
  }

  jvector_free(jkv_t, ht);
}

static int jht_grow(tv* ht) {
  tv new_ht = {.len = ht->len, .capacity = jht_capacity_grow(ht->capacity)};
  new_ht.data = reallocate(0, 0, new_ht.capacity * sizeof(jkv_t));
  if (!new_ht.data) return 0;
  memset(new_ht.data, 0, new_ht.capacity * sizeof(jkv_t));

  // remapping (move)
  for (int i = 0; i < ht->capacity; i++) {
    jkv_t* head = ht->data + i * sizeof(jkv_t);
    while (head->next) {
      jkv_t* entry = head->next;
      head->next = entry->next;

      jkv_t* new_head =
          new_ht.data + jht_index(new_ht, entry->key) * sizeof(jkv_t);
      entry->next = new_head->next;
      new_head->next = entry;
    }
  }

  // everything moved, just delete the dummy head
  jvector_free(jkv_t, ht);

  // just copy the content
  *ht = new_ht;
  return 1;
}

/* ==============================
 *       API IMPLEMENTATION
 * ============================== */

/* ==============================
 *          1. TO_STRING
 * ============================== */

static int jnull_to_string(jnode_t* jnode, tv* jstr);
static int jbool_to_string(jnode_t* jnode, tv* jstr);
static int jnumber_to_string(jnode_t* jnode, tv* jstr);
static int jstring_to_string(jnode_t* jnode, tv* jstr);
static int jarray_to_string(jnode_t* jnode, tv* jstr);
static int jobject_to_string(jnode_t* jnode, tv* jstr);

static int (*jto_strings[])(jnode_t*, tv*) = {
    [JNULL] = jnull_to_string,     [JBOOLEAN] = jbool_to_string,
    [JNUMBER] = jnumber_to_string, [JSTRING] = jstring_to_string,
    [JARRAY] = jarray_to_string,   [JOBJECT] = jobject_to_string,
};

static int jnull_to_string(jnode_t* jnode, tv* jstr) {
  (void)jnode;
  return jvector_concat(char, jstr, "null", 4);
}

static int jbool_to_string(jnode_t* jnode, tv* jstr) {
  jenter();
  check_type(jnode, boolean, 0);
  jbool_t* jbool = jas_bool(jnode);
  if (jbool->value) {
    jleave(jvector_concat(char, jstr, "true", 4));
  } else {
    jleave(jvector_concat(char, jstr, "false", 5));
  }
}

static int jnumber_to_string(jnode_t* jnode, tv* jstr) {
  jenter();
  check_type(jnode, number, 0);
  jnumber_t* jnum = jas_number(jnode);
  char buffer[64];
  int len = sprintf(buffer, "%g", jnum->value);
  jleave(jvector_concat(char, jstr, buffer, len));
}

static int jstring_to_string(jnode_t* jnode, tv* jstr) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstring = jas_string(jnode);
  int len = jvector_len(jstring->string);
  const char *src = jvector_data(jstring->string), *end = src + len;
  if (!jvector_concat(char, jstr, "\"", 1)) jleave(0);
  for (const char *it = src, *next; it < end; it = next) {
    next = strpbrk(it, "\"\\\b\f\n\r\t");
    if (!next) next = end;
    if (!jvector_concat(char, jstr, it, next - it)) jleave(0);
    if (next < end) {
      switch (*next) {
        case '"':
          if (!jvector_concat(char, jstr, "\\\"", 2)) jleave(0);
          break;
        case '\\':
          if (!jvector_concat(char, jstr, "\\\\", 2)) jleave(0);
          break;
        case '\b':
          if (!jvector_concat(char, jstr, "\\b", 2)) jleave(0);
          break;
        case '\f':
          if (!jvector_concat(char, jstr, "\\f", 2)) jleave(0);
          break;
        case '\n':
          if (!jvector_concat(char, jstr, "\\n", 2)) jleave(0);
          break;
        case '\r':
          if (!jvector_concat(char, jstr, "\\r", 2)) jleave(0);
          break;
        case '\t':
          if (!jvector_concat(char, jstr, "\\t", 2)) jleave(0);
          break;
      }
      next++;
    }
  }
  jleave(jvector_concat(char, jstr, "\"", 1));
}

static int jarray_to_string(jnode_t* jnode, tv* jstr) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarray = jas_array(jnode);
  if (!jvector_concat(char, jstr, "[", 1)) jleave(0);

  jnode_t* item = *jvector_get(jarray->array, 0);
  if (!jto_strings[item->type](item, jas_tv(jstr))) jleave(0);
  for (int i = 1; i < jvector_len(jarray->array); i++) {
    jvector_concat(char, jstr, ", ", 2);
    jnode_t* item = *jvector_get(jarray->array, i);
    if (!jto_strings[item->type](item, jas_tv(jstr))) jleave(0);
  }

  jleave(jvector_concat(char, jstr, "]", 1));
}

static int jobject_to_string(jnode_t* jnode, tv* jstr) {
  jenter();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  if (!jvector_concat(char, jstr, "{", 1)) jleave(0);

  // iterating items
  int count = 0;
  for (int i = 0; i < jht_capacity(jobj->hashmap); i++) {
    jkv_t* head = jht_get(jobj->hashmap, i);
    for (const jkv_t* it = head->next; it; it = it->next) {
      count++;

      int len = strlen(it->key);
      if (!jvector_concat(char, jstr, "\"", 1)) jleave(0);
      if (!jvector_concat(char, jstr, it->key, len)) jleave(0);
      if (!jvector_concat(char, jstr, "\": ", 3)) jleave(0);

      jnode_t* item = it->value;
      if (!jto_strings[item->type](item, jas_tv(jstr))) jleave(0);
      if (count < jht_size(jobj->hashmap))
        if (!jvector_concat(char, jstr, ", ", 2)) jleave(0);
    }
  }

  jleave(jvector_concat(char, jstr, "}", 1));
}

char* jto_string(jnode_t* jnode) {
  jenter();
  jvector(char, jstr);
  jvector_init(char, &jstr);
  if (jto_strings[jnode->type](jnode, jas_tv(&jstr))) {
    if (!jvector_concat(char, &jstr, "\0", 1)) jleave(0);
    jleave(jvector_data(jstr));
  } else {
    jleave(0);
  }
}

/* ==============================
 *       2. NODE OPERATION
 * ============================== */

jnode_t* jnull_new() {
  jenter();
  static jnull_t jnull_ = {.type = JNULL};
  jleave(jcast(&jnull_, jnode_t*));
}

jnode_t* jbool_new(int value) {
  jenter();
  static jbool_t jtrue_ = {.type = JBOOLEAN, .value = 1};
  static jbool_t jfalse_ = {.type = JBOOLEAN, .value = 0};
  if (value) {
    jleave(jcast(&jtrue_, jnode_t*));
  } else {
    jleave(jcast(&jfalse_, jnode_t*));
  }
}

jnode_t* jnumber_new(double value) {
  jenter();
  jnumber_t* jnum = reallocate(0, 0, sizeof(jnumber_t));
  if (!jnum) jleave(0);
  jnum->type = JNUMBER;
  jnum->value = value;
  jleave(jcast(jnum, jnode_t*));
}

jnode_t* jstring_new(int len, const char* string) {
  jenter();
  jstring_t* jstr = reallocate(0, 0, sizeof(jstring_t));
  if (!jstr) jleave(0);
  jstr->type = JSTRING;
  jvector_init(char, &jstr->string);
  if (!len) len = strlen(string);
  if (!jvector_concat(char, &jstr->string, string, len)) {
    reallocate(jstr, sizeof(jstring_t), 0);
    jleave(0);
  }
  if (!jvector_concat(char, &jstr->string, "\0", 1)) {
    reallocate(jstr, sizeof(jstring_t), 0);
    jleave(0);
  }
  // jvector_concat would increase the length by 1.
  // We need to pop the last character to make the length correct.
  jvector_pop(char, &jstr->string, 1);
  jleave(jcast(jstr, jnode_t*));
}

jnode_t* jstring_own(char* string) {
  jenter();
  jstring_t* jstr = reallocate(0, 0, sizeof(jstring_t));
  if (!jstr) jleave(0);
  jstr->type = JSTRING;
  jvector_init(char, &jstr->string);
  jvector_len(jstr->string) = strlen(string);
  jvector_capacity(jstr->string) = jvector_len(jstr->string) + 1;
  jvector_data(jstr->string) = string;
  jleave(jcast(jstr, jnode_t*));
}

jnode_t* jarray_new() {
  jenter();
  jarray_t* jarray = reallocate(0, 0, sizeof(jarray_t));
  if (!jarray) jleave(0);
  jarray->type = JARRAY;
  jvector_init(jnode_t, &jarray->array);
  jleave(jcast(jarray, jnode_t*));
}

jnode_t* jobject_new() {
  jenter();
  jobject_t* jobj = reallocate(0, 0, sizeof(jobject_t));
  if (!jobj) jleave(0);
  jobj->type = JOBJECT;
  if (!jht_init(jas_tv(&jobj->hashmap))) {
    reallocate(jobj, sizeof(jobject_t), 0);
    jleave(0);
  }
  jleave(jcast(jobj, jnode_t*));
}

struct jcopy_context {
  int success;
  jnode_t* jnode;
};

static void jcopy_array_item(jnode_t* item, void* userp) {
  struct jcopy_context* ctx = jcast(userp, struct jcopy_context*);
  jnode_t* array = ctx->jnode;
  jnode_t* new_item = jcopy(item);
  if (!new_item) {
    ctx->success = 0;
    return;
  }
  if (!jarray_add(array, new_item)) {
    jdelete(new_item);
  }
  ctx->success = 1;
}

static void jcopy_object_item(const char* key, jnode_t* value, void* userp) {
  struct jcopy_context* ctx = jcast(userp, struct jcopy_context*);
  jnode_t* object = ctx->jnode;
  jnode_t* new_value = jcopy(value);
  if (!new_value) {
    ctx->success = 0;
    return;
  }
  if (!jobject_put(object, key, new_value)) {
    jdelete(new_value);
    ctx->success = 0;
    return;
  }
  ctx->success = 1;
}

jnode_t* jcopy(jnode_t* jnode) {
  jenter();
  if (!jnode) jleave(0);
  switch (jnode->type) {
    case JNULL: jleave(jnull_new());
    case JBOOLEAN: jleave(jbool_new(jas_bool(jnode)->value));
    case JNUMBER: jleave(jnumber_new(jas_number(jnode)->value));
    case JSTRING:
      jleave(jstring_new(0, jvector_data(jas_string(jnode)->string)));
    case JARRAY: {
      jnode_t* new_array = jarray_new();
      if (!new_array) jleave(0);
      struct jcopy_context ctx = {.success = 1, .jnode = new_array};
      jarray_foreach(jnode, jcopy_array_item, &ctx);
      if (!ctx.success) {
        jdelete(new_array);
        jleave(0);
      }
      jleave(new_array);
    }
    case JOBJECT: {
      jnode_t* new_obj = jobject_new();
      if (!new_obj) jleave(0);
      struct jcopy_context ctx = {.success = 1, .jnode = new_obj};
      jobject_foreach(jnode, jcopy_object_item, &ctx);
      if (!ctx.success) {
        jdelete(new_obj);
        jleave(0);
      }
      jleave(new_obj);
    }
  }
  jleave(0);
}

void jdelete(jnode_t* jnode) {
  jenter();
  if (!jnode) jleave();
  switch (jnode->type) {
    case JNULL: break;
    case JBOOLEAN: break;
    case JNUMBER: reallocate(jnode, sizeof(jnumber_t), 0); break;
    case JSTRING: {
      jstring_t* jstr = jas_string(jnode);
      jvector_free(char, &jstr->string);
      reallocate(jstr, sizeof(jstring_t), 0);
      break;
    }
    case JARRAY: {
      jarray_t* jarray = jas_array(jnode);
      jvector_foreach(i, jarray->array) {
        jnode_t* item = *jvector_get(jarray->array, i);
        jdelete(item);
      }
      jvector_free(jnode_t, &jarray->array);
      reallocate(jarray, sizeof(jarray_t), 0);
      break;
    }
    case JOBJECT: {
      jobject_t* jobj = jas_object(jnode);
      jht_free(jas_tv(&jobj->hashmap));
      reallocate(jobj, sizeof(jobject_t), 0);
      break;
    }
  }
  jleave();
}

/* ==============================
 *      3. STRING OPERATION
 * ============================== */

int jstring_len(jnode_t* jnode) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jleave(jvector_len(jstr->string));
}

char jstring_get(jnode_t* jnode, int index) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jleave(*jvector_get(jstr->string, index));
}

const char* jstring_content(jnode_t* jnode) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jleave(jvector_data(jstr->string));
}

int jstring_add(jnode_t* jnode, char c) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jleave(jvector_concat(char, &jstr->string, &c, 1));
}

int jstring_insert(jnode_t* jnode, int index, char c) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jleave(jvector_insert(char, &jstr->string, index, &c, 1));
}

int jstring_concat(jnode_t* jnode, const char* string) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  int len = strlen(string);
  jleave(jvector_concat(char, &jstr->string, string, len));
}

int jstring_pop(jnode_t* jnode) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_pop(char, &jstr->string, 1);
  jleave(1);
}

int jstring_remove(jnode_t* jnode, int index) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_remove(char, &jstr->string, index, 1);
  jleave(1);
}

int jstring_truncate(jnode_t* jnode, int len) {
  jenter();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_pop(char, &jstr->string, jvector_len(jstr->string) - len);
  jleave(1);
}

/* ==============================
 *       4. ARRAY OPERATION
 * ============================== */

int jarray_size(jnode_t* jnode) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jleave(jvector_len(jarr->array));
}

jnode_t* jarray_get(jnode_t* jnode, int index) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jleave(*jvector_get(jarr->array, index));
}

int jarray_add(jnode_t* jnode, jnode_t* value) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jleave(jvector_concat(jnode_t*, &jarr->array, &value, 1));
}

int jarray_insert(jnode_t* jnode, int index, jnode_t* value) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jleave(jvector_insert(jnode_t*, &jarr->array, index, &value, 1));
}

int jarray_pop(jnode_t* jnode) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jnode_t* item = *jvector_pop(jnode_t*, &jarr->array, 1);
  if (item) {
    jdelete(item);
    jleave(1);
  } else {
    jerror_log("Array is empty.");
    jleave(0);
  }
}

int jarray_remove(jnode_t* jnode, int index) {
  jenter();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jnode_t** item = jvector_remove(jnode_t*, &jarr->array, index, 1);
  if (item) {
    jdelete(*item);
    jleave(1);
  } else {
    jleave(0);
  }
}

void jarray_foreach(jnode_t* jnode, void (*f)(jnode_t*, void*), void* userp) {
  jenter();
  check_type(jnode, array, );
  jarray_t* jarr = jas_array(jnode);
  jvector_foreach(i, jarr->array) {
    jnode_t* item = *jvector_get(jarr->array, i);
    f(item, userp);
  }
  jleave();
}

/* ==============================
 *      5. OBJECT OPERATION
 * ============================== */

int jobject_size(jnode_t* jnode) {
  jenter();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  jleave(jht_size(jobj->hashmap));
}

int jobject_has(jnode_t* jnode, const char* key) {
  jenter();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  jkv_t* head = jht_head(jobj->hashmap, key);

  int found = 0, len = 0;
  for (const jkv_t* it = head->next; it; it = it->next) {
    len++;
    if (!strcmp(key, it->key)) {
      found = 0;
      break;
    }
  }

  // remap if load is too high
  if (len > jht_max_len) {
    jht_grow(jas_tv(&jobj->hashmap));
  }

  jleave(found);
}

jnode_t* jobject_get(jnode_t* jnode, const char* key) {
  jenter();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  jkv_t* head = jht_head(jobj->hashmap, key);

  int len = 0;
  const jkv_t* found = 0;
  for (const jkv_t* it = head->next; it; it = it->next) {
    len++;
    if (!strcmp(key, it->key)) {
      found = it;
    }
  }

  if (!found) {
    jerror_log("Key '%s' not exists.", key);
    jleave(0);
  }

  // remap if load is too high
  if (len > jht_max_len) {
    jht_grow(jas_tv(&jobj->hashmap));
  }

  jleave(found->value);
}

int jobject_put(jnode_t* jnode, const char* key, jnode_t* value) {
  jenter();
  if (!key) {
    jerror_log("Null key.");
    jleave(0);
  }

  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  jkv_t* head = jht_head(jobj->hashmap, key);

  int len = 0;
  jkv_t *target = 0, *prev = head;
  for (jkv_t* it = head->next; it; prev = it, it = it->next) {
    len++;
    if (!strcmp(key, it->key)) {
      target = it;
      break;
    }
  }

  int changed = 0;
  if (target) {
    if (value) {
      // update
      jnode_t* old = target->value;
      target->value = value;
      jdelete(old);
      changed = 1;
    } else {
      // erase
      prev->next = target->next;
      jdelete(target->value);
      reallocate(target->key, 0, 0);
      reallocate(target, sizeof(jkv_t), 0);

      jht_size(jobj->hashmap)--;
      changed = 1;
    }
  } else {
    if (value) {
      // add
      jkv_t* new = reallocate(0, 0, sizeof(jkv_t));
      if (!new) jleave(0);
      new->value = value;
      int len = strlen(key);
      new->key = reallocate(0, 0, len + 1);
      if (!new->key) {
        reallocate(new, sizeof(jkv_t), 0);
        jleave(0);
      }
      strcpy(new->key, key);

      // prepend
      new->next = head->next;
      head->next = new;

      jht_size(jobj->hashmap)++;
      changed = 1;
    } else {
      // do nothing
      jerror_log("Null key and null value.");
      changed = 0;
    }
  }

  // remap if load is too high
  if (len > jht_max_len) {
    jht_grow(jas_tv(&jobj->hashmap));
  }

  jleave(changed);
}

void jobject_foreach(jnode_t* jnode, void (*f)(const char*, jnode_t*, void*),
                     void* userp) {
  jenter();
  check_type(jnode, object, );
  jobject_t* jobj = jas_object(jnode);
  for (int i = 0; i < jht_capacity(jobj->hashmap); i++) {
    jkv_t* head = jht_get(jobj->hashmap, i);
    for (jkv_t* it = head->next; it; it = it->next) {
      f(it->key, it->value, userp);
    }
  }
  jleave();
}

/* ==============================
 *          6. FROM_STRING
 * ============================== */

/* ==============================
 *          6.1 LEXING
 * ============================== */

#define jlexer_linecol_str " at line %d, column %d."
#define jlexer_linecol(lexer) (lexer)->line, (lexer)->col
#define jlexer_ptr(lexer, index) ((lexer)->data + (index))
#define jlexer_currptr(lexer) jlexer_ptr((lexer), (lexer)->curr)
#define jlexer_look(lexer, offset) \
  (*jlexer_ptr((lexer), (lexer)->curr + (offset)))
#define jlexer_peek(lexer) jlexer_look((lexer), 0)
#define jlexer_match(lexer, c) (jlexer_peek(lexer) == (c))
#define jlexer_move(lexer, distance)                            \
  do {                                                          \
    for (int i = 0; i < (distance); i++) jlexer_advance(lexer); \
  } while (0)
#define jlexer_rest(lexer) ((lexer)->len - (lexer)->curr)
#define jlexer_is_end(lexer) ((lexer)->curr >= (lexer)->len)
#define jlexer_to_token(lexer, name, type_, len_, ...) \
  jtoken_t name = {.line = (lexer)->line,              \
                   .col = (lexer)->col,                \
                   .type = (type_),                    \
                   .len = (len_),                      \
                   .lexeme = jlexer_currptr(lexer),    \
                   ##__VA_ARGS__}

#define jtoken_linecol_str " at line %d, column %d."
#define jtoken_linecol(token) (token)->line, (token)->col
#define jtoken_lenlexeme(token) (token)->len, (token)->lexeme

enum jtktype {
  JTK_NULL = 256,
  JTK_TRUE,
  JTK_FALSE,
  JTK_NUMBER,
  JTK_STRING,
  JTK_EOF,
};

typedef struct jtoken {
  int line;
  int col;
  int type;
  int len;
  const char* lexeme;
  union {
    double number;
    const char* string;
  } as;
} jtoken_t;

typedef struct jlexer {
  int len;
  int curr;
  int line;
  int col;
  const char* data;
} jlexer_t;

static void jlexer_advance(jlexer_t* lexer) {
  if (jlexer_match(lexer, '\n')) {
    lexer->line++;
    lexer->col = 0;
  } else {
    lexer->col++;
  }
  lexer->curr++;
}

static void jlexer_skip_blank(jlexer_t* lexer) {
  while (!jlexer_is_end(lexer) &&
         (isblank(jlexer_peek(lexer)) || iscntrl(jlexer_peek(lexer))))
    jlexer_advance(lexer);
}

static int jlex_keyword(jlexer_t* lexer, tv* tokens, int type,
                        const char* keyword) {
  int len = strlen(keyword);
  if (jlexer_rest(lexer) < len) {
    jerror_log("Insufficient input for lexing" jlexer_linecol_str,
               jlexer_linecol(lexer));
    return 0;
  }
  if (!strncmp(jlexer_currptr(lexer), keyword, len)) {
    jlexer_to_token(lexer, tk, type, len);
    if (!jvector_concat(jtoken_t, tokens, &tk, 1)) return 0;
    jlexer_move(lexer, len);
    return 1;
  } else {
    jerror_log("Expect keyword '%s' but got '%.8s'" jlexer_linecol_str, keyword,
               jlexer_currptr(lexer), jlexer_linecol(lexer));
    return 0;
  }
}

static int jlex_number(jlexer_t* lexer, tv* tokens) {
  char* end = 0;
  double val = strtod(jlexer_currptr(lexer), &end);
  int len = end - jlexer_currptr(lexer);
  if (!len) {
    jerror_log("Unknown number format '%.8s'" jlexer_linecol_str,
               jlexer_currptr(lexer), jlexer_linecol(lexer));
    return 0;
  }

  jlexer_to_token(lexer, tk, JTK_NUMBER, len, .as.number = val);
  if (!jvector_concat(jtoken_t, tokens, &tk, 1)) return 0;
  jlexer_move(lexer, len);
  return 1;
}

static int jlex_string(jlexer_t* lexer, tv* tokens) {
  if (!jlexer_match(lexer, '\"')) {
    jerror_log("Expect \" but got '%c'" jlexer_linecol_str, jlexer_peek(lexer),
               jlexer_linecol(lexer));
    return 0;
  }
  jlexer_to_token(lexer, tk, JTK_STRING, 0);
  jlexer_advance(lexer);
  tk.len++;
  tk.as.string = jlexer_currptr(lexer);
  while (!jlexer_is_end(lexer) && !jlexer_match(lexer, '\"') &&
         !jlexer_match(lexer, '\n')) {
    if (jlexer_match(lexer, '\\')) {
      tk.len++;
      jlexer_advance(lexer);
      if (jlexer_is_end(lexer)) {
        jerror_log("Unexpected end of input" jlexer_linecol_str,
                   jlexer_linecol(lexer));
        return 0;
      }
    }
    tk.len++;
    jlexer_advance(lexer);
  }
  if (!jlexer_match(lexer, '\"')) {
    jerror_log("Expect \" but got '%c'" jlexer_linecol_str, jlexer_peek(lexer),
               jlexer_linecol(lexer));
    return 0;
  }
  jlexer_advance(lexer);
  tk.len++;
  return jvector_concat(jtoken_t, tokens, &tk, 1);
}

static int jlex(jlexer_t* lexer, tv* tokens) {
  for (jlexer_skip_blank(lexer); !jlexer_is_end(lexer);
       jlexer_skip_blank(lexer)) {
    switch (jlexer_peek(lexer)) {
      case 'n': {
        if (!jlex_keyword(lexer, tokens, JTK_NULL, "null")) return 0;
        break;
      }

      case 't': {
        if (!jlex_keyword(lexer, tokens, JTK_TRUE, "true")) return 0;
        break;
      }

      case 'f': {
        if (!jlex_keyword(lexer, tokens, JTK_FALSE, "false")) return 0;
        break;
      }

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '+':
      case '-': {
        if (!jlex_number(lexer, tokens)) return 0;
        break;
      }

      case '\"': {
        if (!jlex_string(lexer, tokens)) return 0;
        break;
      }

      case '[':
      case ']':
      case '{':
      case '}':
      case ',':
      case ':': {
        jlexer_to_token(lexer, tk, jlexer_peek(lexer), 1);
        if (!jvector_concat(jtoken_t, tokens, &tk, 1)) return 0;
        jlexer_advance(lexer);
        break;
      }

      default: {
        // Unrecognizable character
        jerror_log("Unrecognizable character '%c'" jlexer_linecol_str,
                   jlexer_peek(lexer), jlexer_linecol(lexer));
        return 0;
      }
    }
  }
  jlexer_to_token(lexer, tk, JTK_EOF, 0);
  return jvector_concat(jtoken_t, tokens, &tk, 1);
}

/* ==============================
 *          6.2 PARSING
 * ============================== */

#define jparser_ptr(parser, index) ((parser)->data + (index))
#define jparser_currptr(parser) jparser_ptr((parser), (parser)->curr)
#define jparser_match(parser, type_) (jparser_currptr(parser)->type == (type_))
#define jparser_advance(parser) ((parser)->curr++)
#define jparser_is_end(parser) jparser_match((parser), JTK_EOF)
#define jparse_end_return ((void*)-1)

/* The caller must guarantee that the dst buffer is large enough to hold the
 * normalized string. */
static int jnormalize_string(char* dst, const char* src, int len) {
  const char* escape = strchr(src, '\\');
  if (!escape || escape >= src + len) {
    strncpy(dst, src, len);
    dst[len] = '\0';
    return len;
  }
  int dst_len = 0;
  const char* end = src + len;
  for (const char *it = src, *next; it < end; it = next) {
    next = strchr(it, '\\');
    if (!next || next >= end) next = end;
    int copy_len = next - it;
    strncpy(dst, it, copy_len);
    dst += copy_len;
    dst_len += copy_len;
    if (next < end) {
      next++;
      switch (*next) {
        case '\"': *dst++ = '\"'; break;
        case '\\': *dst++ = '\\'; break;
        case '/': *dst++ = '/'; break;
        case 'b': *dst++ = '\b'; break;
        case 'f': *dst++ = '\f'; break;
        case 'n': *dst++ = '\n'; break;
        case 'r': *dst++ = '\r'; break;
        case 't': *dst++ = '\t'; break;
        default: jerror_log("Unknown escape character '%c'", *next); return 0;
      }
      next++;
      dst_len++;
    }
  }
  *dst = '\0';
  return dst_len;
}

typedef struct jparser {
  int len;
  int curr;
  const jtoken_t* data;
} jparser_t;

static jnode_t* jparse(jparser_t* parser);

static jnode_t* jstring_from_token(const jtoken_t* tk) {
  int len = tk->len - 2;
  if (len <= 2) return jstring_new(0, "");
  const char* src = tk->as.string;
  jnode_t* string = jstring_new(len, src);
  const char* escape = strchr(src, '\\');
  if (escape && escape < src + len) {
    jstring_t* jstr = jas_string(string);
    char* dst = jvector_data(jstr->string);
    if (!(jvector_len(jstr->string) = jnormalize_string(dst, src, len))) {
      jdelete(string);
      return 0;
    }
  }
  return string;
}

static jnode_t* jparse_array(jparser_t* parser) {
  jnode_t* array = jarray_new();
  if (jparser_match(parser, ']')) {
    jparser_advance(parser);
    return array;
  }

  // parse first item
  jnode_t* first_item = jparse(parser);
  if (!first_item || first_item == jparse_end_return) {
    jdelete(array);
    return 0;
  }
  if (!jarray_add(array, first_item)) return 0;

  // parse rest items
  while (!jparser_is_end(parser) && !jparser_match(parser, ']')) {
    if (!jparser_match(parser, ',')) {
      const jtoken_t* tk = jparser_currptr(parser);
      jerror_log("Expect ',' but got '%.*s'" jtoken_linecol_str,
                 jtoken_lenlexeme(tk), jtoken_linecol(tk));
      jdelete(array);
      return 0;
    }
    jparser_advance(parser);

    jnode_t* item = jparse(parser);
    if (!item || item == jparse_end_return) {
      jdelete(array);
      return 0;
    }
    if (!jarray_add(array, item)) return 0;
  }

  if (jparser_is_end(parser) || !jparser_match(parser, ']')) {
    const jtoken_t* tk = jparser_currptr(parser);
    jerror_log("Expect ']' but got '%.*s'" jtoken_linecol_str,
               jtoken_lenlexeme(tk), jtoken_linecol(tk));
    jdelete(array);
    return 0;
  }
  jparser_advance(parser);  // discard ']'

  return array;
}

/* Key copy lexemes. Value is allocated. When key is 0, error happens. */
static jkv_t jparse_keyvalue(jparser_t* parser) {
  jkv_t kv = {};

  if (!jparser_match(parser, JTK_STRING)) {
    const jtoken_t* tk = jparser_currptr(parser);
    jerror_log("Expect a string but got '%.*s'" jtoken_linecol_str,
               jtoken_lenlexeme(tk), jtoken_linecol(tk));
    return kv;
  }
  const jtoken_t* key = jparser_currptr(parser);
  jparser_advance(parser);

  if (!jparser_match(parser, ':')) {
    const jtoken_t* tk = jparser_currptr(parser);
    jerror_log("Expect ':' but got '%.*s'" jtoken_linecol_str,
               jtoken_lenlexeme(tk), jtoken_linecol(tk));
    return kv;
  }
  jparser_advance(parser);

  jnode_t* value = jparse(parser);
  if (!value || value == jparse_end_return) return kv;

  kv.key = reallocate(kv.key, 0, key->len - 1);
  if (!kv.key) return kv;
  if (!jnormalize_string(kv.key, key->as.string, key->len - 2)) {
    kv.key = reallocate(kv.key, key->len - 1, 0);
    jdelete(value);
    return kv;
  }
  kv.value = value;
  return kv;
}

static jnode_t* jparse_object(jparser_t* parser) {
  jnode_t* obj = jobject_new();
  if (jparser_match(parser, '}')) {
    jparser_advance(parser);
    return obj;
  }

  // parse first key value
  jkv_t first_kv = jparse_keyvalue(parser);
  if (!first_kv.key) {
    jdelete(obj);
    return 0;
  }
  jobject_put(obj, first_kv.key, first_kv.value);
  reallocate(first_kv.key, 0, 0);  // free key after copy

  // parse rest key values
  while (!jparser_is_end(parser) && !jparser_match(parser, '}')) {
    if (!jparser_match(parser, ',')) {
      const jtoken_t* tk = jparser_currptr(parser);
      jerror_log("Expect ',' but got '%.*s'" jtoken_linecol_str,
                 jtoken_lenlexeme(tk), jtoken_linecol(tk));
      jdelete(obj);
      return 0;
    }
    jparser_advance(parser);

    jkv_t kv = jparse_keyvalue(parser);
    if (!kv.key) {
      jdelete(obj);
      return 0;
    }
    jobject_put(obj, kv.key, kv.value);
    reallocate(kv.key, 0, 0);  // key is copied
  }

  if (jparser_is_end(parser) || !jparser_match(parser, '}')) {
    const jtoken_t* tk = jparser_currptr(parser);
    jerror_log("Expect '}' but got '%.*s'" jtoken_linecol_str,
               jtoken_lenlexeme(tk), jtoken_linecol(tk));
    jdelete(obj);
    return 0;
  }
  jparser_advance(parser);

  return obj;
}

static jnode_t* jparse(jparser_t* parser) {
  const jtoken_t* tk = jparser_currptr(parser);
  jparser_advance(parser);
  switch (tk->type) {
    case JTK_NULL: return jnull_new();
    case JTK_TRUE: return jbool_new(1);
    case JTK_FALSE: return jbool_new(0);
    case JTK_NUMBER: return jnumber_new(tk->as.number);
    case JTK_STRING: return jstring_from_token(tk);
    case JTK_EOF: return jparse_end_return;  // End of parsing
    case '[': return jparse_array(parser);
    case '{': return jparse_object(parser);

    case ']':
    case '}':
    case ',':
    case ':':           // Should be handled in specific functions
    default: return 0;  // Unknown token
  }
}

jnode_t* jfrom_string(const char* json_str) {
  jenter();
  jlexer_t lexer = {.len = strlen(json_str),
                    .curr = 0,
                    .line = 1,
                    .col = 1,
                    .data = json_str};

  // lexing
  jvector(jtoken_t, tokens);
  jvector_init(jtoken_t, &tokens);
  if (!jlex(&lexer, jas_tv(&tokens))) {
    jvector_free(jtoken_t, &tokens);
    jleave(0);
  }
  // jvector_foreach(i, tokens) {
  //   jtoken_t* tk = jvector_get(tokens, i);
  //   printf("[%04d:%04d](%04d) %.*s\n", tk->line, tk->col, tk->type, tk->len,
  //          tk->lexeme);
  // }

  // parsing
  jparser_t parser = {.len = tokens.len, .curr = 0, .data = tokens.data};
  jnode_t* json = jparse(&parser);

  jvector_free(jtoken_t, &tokens);
  jleave(json);
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sjson.h>

/* ==========================
 *       ERROR OPERATION
 * ========================== */

#define MSG_BUFFER_LEN 1024
#define jerror_clear() \
  do {                 \
    has_err = 0;       \
    err_msg[0] = 0;    \
  } while (0)
#define jerror_log(fmt, ...)                \
  do {                                      \
    has_err = 1;                            \
    sprintf(err_msg, (fmt), ##__VA_ARGS__); \
  } while (0)

static int has_err = 0;
static char err_msg[MSG_BUFFER_LEN];

const char* jerror() { return has_err ? err_msg : 0; }

/* =======================
 *          UTILS
 * ======================= */

// #define jabort(fmt, ...)                      \
//   do {                                        \
//     fprintf(stderr, fmt "\n", ##__VA_ARGS__); \
//     *(char*)0;                                \
//   } while (0)
// #define jassert(expr, fmt, ...)              \
//   do {                                       \
//     if (!(expr)) jabort(fmt, ##__VA_ARGS__); \
//   } while (0)
#define check_type(node, type, ret, ...)                      \
  do {                                                        \
    if (!jis_##type(node)) {                                  \
      jerror_log("Expect type '%s' but got type '%s'", #type, \
                 type_str[jtype(node)]);                      \
      ##__VA_ARGS__ return ret;                               \
    }                                                         \
  } while (0)
#define grow_capacity(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

static const char* type_str[] = {
    [JNULL] = "null",     [JBOOLEAN] = "boolean", [JNUMBER] = "number",
    [JSTRING] = "string", [JARRAY] = "array",     [JOBJECT] = "object",
};

static void* reallocate(void* ptr, int old, int new) {
  jerror_clear();
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
  jerror_clear();
  v->len = v->capacity = 0;
  v->data = 0;
}

static void tvector_free(tv* v) {
  jerror_clear();
  reallocate(v->data, 0, 0);
}

static int tvector_add(tv* v, const void* value, int len, int typesz) {
  jerror_clear();
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
  jerror_clear();
  v->len -= len;
  if (v->len < 0) v->len = 0;
  return v->data + v->len * typesz;
}

static int tvector_right_shift(tv* v, int index, int distance, int typesz) {
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
  jvector_init(jkv_t, ht);
  ht->capacity = jht_capacity_grow(ht->capacity);
  int new = ht->capacity * sizeof(jkv_t);
  ht->data = reallocate(0, 0, new);
  if (!ht->data) return 0;
  memset(ht->data, 0, new);
  return 1;
}

static void jht_free(tv* ht) {
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
  return jvector_concat(char, jstr, "null", 4);
}

static int jbool_to_string(jnode_t* jnode, tv* jstr) {
  jerror_clear();
  check_type(jnode, boolean, 0);
  jbool_t* jbool = jas_bool(jnode);
  if (jbool->value) {
    return jvector_concat(char, jstr, "true", 4);
  } else {
    return jvector_concat(char, jstr, "false", 5);
  }
}

static int jnumber_to_string(jnode_t* jnode, tv* jstr) {
  jerror_clear();
  check_type(jnode, number, 0);
  jnumber_t* jnum = jas_number(jnode);
  char buffer[64];
  int len = sprintf(buffer, "%g", jnum->value);
  return jvector_concat(char, jstr, buffer, len);
}

static int jstring_to_string(jnode_t* jnode, tv* jstr) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstring = jas_string(jnode);
  if (!jvector_concat(char, jstr, "\"", 1)) return 0;
  if (!jvector_concat(char, jstr, jvector_data(jstring->string),
                      jvector_len(jstring->string)))
    return 0;
  return jvector_concat(char, jstr, "\"", 1);
}

static int jarray_to_string(jnode_t* jnode, tv* jstr) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarray = jas_array(jnode);
  if (!jvector_concat(char, jstr, "[", 1)) return 0;

  jnode_t* item = *jvector_get(jarray->array, 0);
  if (!jto_strings[item->type](item, jas_tv(jstr))) return 0;
  for (int i = 1; i < jvector_len(jarray->array); i++) {
    jvector_concat(char, jstr, ", ", 2);
    jnode_t* item = *jvector_get(jarray->array, i);
    if (!jto_strings[item->type](item, jas_tv(jstr))) return 0;
  }

  return jvector_concat(char, jstr, "]", 1);
}

static int jobject_to_string(jnode_t* jnode, tv* jstr) {
  jerror_clear();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  if (!jvector_concat(char, jstr, "{", 1)) return 0;

  // iterating items
  int count = 0;
  for (int i = 0; i < jht_capacity(jobj->hashmap); i++) {
    jkv_t* head = jht_get(jobj->hashmap, i);
    for (const jkv_t* it = head->next; it; it = it->next) {
      count++;

      int len = strlen(it->key);
      if (!jvector_concat(char, jstr, "\"", 1)) return 0;
      if (!jvector_concat(char, jstr, it->key, len)) return 0;
      if (!jvector_concat(char, jstr, "\": ", 3)) return 0;

      jnode_t* item = it->value;
      if (!jto_strings[item->type](item, jas_tv(jstr))) return 0;
      if (count < jht_size(jobj->hashmap))
        if (!jvector_concat(char, jstr, ", ", 2)) return 0;
    }
  }

  return jvector_concat(char, jstr, "}", 1);
}

char* jto_string(jnode_t* jnode) {
  jerror_clear();
  jvector(char, jstr);
  jvector_init(char, &jstr);
  if (jto_strings[jnode->type](jnode, jas_tv(&jstr))) {
    if (!jvector_concat(char, &jstr, "\0", 1)) return 0;
    return jvector_data(jstr);
  } else {
    return 0;
  }
}

/* ==============================
 *       2. NODE OPERATION
 * ============================== */

jnode_t* jnull_new() {
  jerror_clear();
  static jnull_t jnull_ = {.type = JNULL};
  return jcast(&jnull_, jnode_t*);
}

jnode_t* jbool_new(int value) {
  jerror_clear();
  static jbool_t jtrue_ = {.type = JBOOLEAN, .value = 1};
  static jbool_t jfalse_ = {.type = JBOOLEAN, .value = 0};
  if (value) {
    return jcast(&jtrue_, jnode_t*);
  } else {
    return jcast(&jfalse_, jnode_t*);
  }
}

jnode_t* jnumber_new(double value) {
  jerror_clear();
  jnumber_t* jnum = reallocate(0, 0, sizeof(jnumber_t));
  if (!jnum) return 0;
  jnum->type = JNUMBER;
  jnum->value = value;
  return jcast(jnum, jnode_t*);
}

jnode_t* jstring_new(int len, const char* string) {
  jerror_clear();
  jstring_t* jstr = reallocate(0, 0, sizeof(jstring_t));
  if (!jstr) return 0;
  jstr->type = JSTRING;
  jvector_init(char, &jstr->string);
  if (!len) len = strlen(string);
  if (!jvector_concat(char, &jstr->string, string, len)) {
    reallocate(jstr, sizeof(jstring_t), 0);
    return 0;
  }
  return jcast(jstr, jnode_t*);
}

jnode_t* jarray_new() {
  jerror_clear();
  jarray_t* jarray = reallocate(0, 0, sizeof(jarray_t));
  if (!jarray) return 0;
  jarray->type = JARRAY;
  jvector_init(jnode_t, &jarray->array);
  return jcast(jarray, jnode_t*);
}

jnode_t* jobject_new() {
  jerror_clear();
  jobject_t* jobj = reallocate(0, 0, sizeof(jobject_t));
  if (!jobj) return 0;
  jobj->type = JOBJECT;
  if (!jht_init(jas_tv(&jobj->hashmap))) {
    reallocate(jobj, sizeof(jobject_t), 0);
    return 0;
  }
  return jcast(jobj, jnode_t*);
}

void jdelete(jnode_t* jnode) {
  jerror_clear();
  if (!jnode) return;
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
}

/* ==============================
 *      3. STRING OPERATION
 * ============================== */

int jstring_len(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  return jvector_len(jstr->string);
}

char jstring_get(jnode_t* jnode, int index) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  return *jvector_get(jstr->string, index);
}

const char* jstring_content(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  return jvector_data(jstr->string);
}

int jstring_add(jnode_t* jnode, char c) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  return jvector_concat(char, &jstr->string, &c, 1);
}

int jstring_insert(jnode_t* jnode, int index, char c) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  return jvector_insert(char, &jstr->string, index, &c, 1);
}

int jstring_concat(jnode_t* jnode, const char* string) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  int len = strlen(string);
  return jvector_concat(char, &jstr->string, string, len);
}

int jstring_pop(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_pop(char, &jstr->string, 1);
  return 1;
}

int jstring_remove(jnode_t* jnode, int index) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_remove(char, &jstr->string, index, 1);
  return 1;
}

int jstring_truncate(jnode_t* jnode, int len) {
  jerror_clear();
  check_type(jnode, string, 0);
  jstring_t* jstr = jas_string(jnode);
  jvector_pop(char, &jstr->string, jvector_len(jstr->string) - len);
  return 1;
}

/* ==============================
 *       4. ARRAY OPERATION
 * ============================== */

int jarray_size(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  return jvector_len(jarr->array);
}

jnode_t* jarray_get(jnode_t* jnode, int index) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  return *jvector_get(jarr->array, index);
}

int jarray_add(jnode_t* jnode, jnode_t* value) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  return jvector_concat(jnode_t*, &jarr->array, &value, 1);
}

int jarray_insert(jnode_t* jnode, int index, jnode_t* value) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  return jvector_insert(jnode_t*, &jarr->array, index, &value, 1);
}

int jarray_pop(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jnode_t* item = *jvector_pop(jnode_t*, &jarr->array, 1);
  if (item) {
    jdelete(item);
    return 1;
  } else {
    jerror_log("Array is empty.");
    return 0;
  }
}

int jarray_remove(jnode_t* jnode, int index) {
  jerror_clear();
  check_type(jnode, array, 0);
  jarray_t* jarr = jas_array(jnode);
  jnode_t** item = jvector_remove(jnode_t*, &jarr->array, index, 1);
  if (item) {
    jdelete(*item);
    return 1;
  } else {
    return 0;
  }
}

void jarray_foreach(jnode_t* jnode, void (*f)(jnode_t*)) {
  jerror_clear();
  check_type(jnode, array, );
  jarray_t* jarr = jas_array(jnode);
  jvector_foreach(i, jarr->array) {
    jnode_t* item = *jvector_get(jarr->array, i);
    f(item);
  }
}

/* ==============================
 *      5. OBJECT OPERATION
 * ============================== */

int jobject_size(jnode_t* jnode) {
  jerror_clear();
  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  return jht_size(jobj->hashmap);
}

int jobject_has(jnode_t* jnode, const char* key) {
  jerror_clear();
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

  return found;
}

jnode_t* jobject_get(jnode_t* jnode, const char* key) {
  jerror_clear();
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
    return 0;
  }

  // remap if load is too high
  if (len > jht_max_len) {
    jht_grow(jas_tv(&jobj->hashmap));
  }

  return found->value;
}

int jobject_put(jnode_t* jnode, const char* key, jnode_t* value) {
  jerror_clear();
  if (!key) {
    jerror_log("Null key.");
    return 0;
  }

  check_type(jnode, object, 0);
  jobject_t* jobj = jas_object(jnode);
  int hash = fnv1a(key);
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
      if (!new) return 0;
      new->value = value;
      int len = strlen(key);
      new->key = reallocate(0, 0, len + 1);
      if (!new->key) {
        reallocate(new, sizeof(jkv_t), 0);
        return 0;
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

  return changed;
}

void jobject_foreach(jnode_t* jnode, void (*f)(const char*, jnode_t*)) {
  jerror_clear();
  check_type(jnode, object, );
  jobject_t* jobj = jas_object(jnode);
  for (int i = 0; i < jht_capacity(jobj->hashmap); i++) {
    jkv_t* head = jht_get(jobj->hashmap, i);
    for (jkv_t* it = head->next; it; it = it->next) {
      f(it->key, it->value);
    }
  }
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
  jerror_clear();
  while (!jlexer_is_end(lexer) &&
         (isblank(jlexer_peek(lexer)) || iscntrl(jlexer_peek(lexer))))
    jlexer_advance(lexer);
}

static int jlex_keyword(jlexer_t* lexer, tv* tokens, int type,
                        const char* keyword) {
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
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
  jerror_clear();
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

typedef struct jparser {
  int len;
  int curr;
  const jtoken_t* data;
} jparser_t;

static jnode_t* jparse(jparser_t* parser);

static jnode_t* jparse_array(jparser_t* parser) {
  jerror_clear();
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
  jerror_clear();
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
  strncpy(kv.key, key->as.string, key->len - 2);
  kv.value = value;
  return kv;
}

static jnode_t* jparse_object(jparser_t* parser) {
  jerror_clear();
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

  if (jparser_is_end(parser) && !jparser_match(parser, '}')) {
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
  jerror_clear();
  const jtoken_t* tk = jparser_currptr(parser);
  jparser_advance(parser);
  switch (tk->type) {
    case JTK_NULL: return jnull_new();
    case JTK_TRUE: return jbool_new(1);
    case JTK_FALSE: return jbool_new(0);
    case JTK_NUMBER: return jnumber_new(tk->as.number);
    case JTK_STRING: {
      if (tk->len <= 2) return jstring_new(0, "");
      else return jstring_new(tk->len - 2, tk->as.string);
    }
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
  jerror_clear();
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
    return 0;
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
  return json;
}
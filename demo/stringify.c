#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

#define println(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

void test_null() {
  jnode_t* null1 = jnull_new();
  jnode_t* null2 = jnull_new();

  println("==== JSON NULL ====");
  char* str1 = jto_string(null1);
  char* str2 = jto_string(null2);
  println("null1: %s [%p]", str1, null1);
  println("null2: %s [%p]", str2, null2);

  free(str1);
  free(str2);
  jdelete(null1);
  jdelete(null2);
}

void test_boolean() {
  jnode_t* jtrue = jbool_new(1);
  jnode_t* jfalse = jbool_new(0);

  println("==== JSON BOOL ====");
  char* str1 = jto_string(jtrue);
  char* str2 = jto_string(jfalse);
  println("jtrue : %5s [%p]", str1, jtrue);
  println("jfalse: %5s [%p]", str2, jfalse);

  free(str1);
  free(str2);
  jdelete(jtrue);
  jdelete(jfalse);
}

void test_number() {
  jnode_t* jint = jnumber_new(6);
  jnode_t* jfloat = jnumber_new(3.14);

  println("==== JSON NUMBER ====");
  char* str1 = jto_string(jint);
  char* str2 = jto_string(jfloat);
  println("jint  : %5s [%p]", str1, jint);
  println("jfloat: %5s [%p]", str2, jfloat);

  free(str1);
  free(str2);
  jdelete(jint);
  jdelete(jfloat);
}

void test_string() {
  const char* str = "Hello JSON!";
  jnode_t* jstr = jstring_new(0, str);

  println("==== JSON NUMBER ====");
  char* str1 = jto_string(jstr);
  println("jstr: %s [%p]", str1, jstr);

  free(str1);
  jdelete(jstr);
}

void test_array() {
  jnode_t* jint = jnumber_new(6);
  jnode_t* jfloat1 = jnumber_new(3.14);
  jnode_t* jfloat2 = jnumber_new(1.414);
  jnode_t* jfloat3 = jnumber_new(2.7);
  jnode_t* jarray = jarray_new();

  jarray_add(jarray, jint);
  jarray_add(jarray, jfloat1);
  jarray_add(jarray, jfloat2);
  jarray_add(jarray, jfloat3);

  println("==== JSON ARRAY ====");
  char* str1 = jto_string(jarray);
  println("jarray: %s [%p]", str1, jarray);

  free(str1);
  jdelete(jarray);
}

void test_object() {
  /*
  {
    "name": "Zywoo",
    "age": 25,
    "rating": 1.43,
    "matches": [
      "EPL",
      "Major",
      "IEM"
    ]
  }
  */
  jnode_t* name = jstring_new(0, "Zywoo");
  jnode_t* age = jnumber_new(25);
  jnode_t* rating = jnumber_new(1.43);
  jnode_t* matches = jarray_new();
  {
    jarray_add(matches, jstring_new(0, "EPL"));
    jarray_add(matches, jstring_new(0, "Major"));
    jarray_add(matches, jstring_new(0, "IEM"));
  }

  jnode_t* pro = jobject_new();
  {
    jobject_put(pro, "name", name);
    jobject_put(pro, "age", age);
    jobject_put(pro, "rating", rating);
    jobject_put(pro, "matches", matches);
  }

  println("==== JSON OBJECT ====");
  char* str1 = jto_string(pro);
  println("jobject: %s [%p]", str1, pro);
  free(str1);

  {
    jobject_put(pro, "rating", jnumber_new(1.51));
    jnode_t* array = jobject_get(pro, "matches");
    jarray_add(array, jstring_new(0, "BLAST"));
  }

  char* str2 = jto_string(pro);
  println("jobject: %s [%p]", str2, pro);
  free(str2);

  {
    jobject_put(pro, "rating", 0);
  }

  char* str3 = jto_string(pro);
  println("jobject: %s [%p]", str3, pro);
  free(str3);

  jdelete(pro);
}

void test_large_object() {
  jnode_t* obj = jobject_new();

  // create large object
  srand(0xAABBCCDD);
  char buffer[1024] = {};
  const char* alphabet =
      "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int i = 0; i < 4096; i++) {
    // generate random string
    int len = rand() % 16 + 1;
    for (int j = 0; j < len; j++) {
      buffer[j] = alphabet[rand() % 62];
    }

    jobject_put(obj, buffer, jstring_new(len, buffer));
  }

  println("==== JSON LARGE OBJECT ====");
  char* str1 = jto_string(obj);
  println("jobject: %.128s... [%p]", str1, obj);
  free(str1);

  jdelete(obj);
}

int main() {
  test_null();
  test_boolean();
  test_number();
  test_string();
  test_array();
  test_object();
  test_large_object();
  return 0;
}
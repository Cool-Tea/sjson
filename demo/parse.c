#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

#define println(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

int main() {
  const char* json_path = "demo/example.json";

  FILE* jp = fopen(json_path, "r");
  if (!jp) {
    println("Failed to open file: %s", json_path);
    return EXIT_FAILURE;
  }

  fseek(jp, 0, SEEK_END);
  int len = ftell(jp);
  fseek(jp, 0, SEEK_SET);
  char* buffer = malloc(len + 1);
  fread(buffer, sizeof(char), len, jp);

  jnode_t* json = jfrom_string(buffer);
  free(buffer);
  if (!json) {
    println("Failed to load json");
    return EXIT_FAILURE;
  }

  char* str1 = jto_string(json);
  println("==== JSON READ ====");
  println("[%s] %s", json_path, str1);
  free(str1);

  {
    jnode_t* arr = jobject_get(json, "array");
    jarray_add(arr, jnumber_new(4));
    jarray_remove(arr, 2);
  }
  jobject_put(json, "boolean", jbool_new(0));
  jobject_put(json, "color", jstring_new(0, "cyan"));
  {
    jnode_t* obj = jobject_get(json, "object");
    jobject_put(obj, "e", jstring_new(0, "f"));
  }

  char* str2 = jto_string(json);
  println("==== JSON MODIFIED ====");
  println("[%s] %s", json_path, str2);
  free(str2);

  jdelete(json);

  return 0;
}
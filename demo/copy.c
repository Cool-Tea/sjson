#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

#define println(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

int main() {
  const char* json_path = "demo/llm.json";

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
  buffer[len] = '\0';
  fclose(jp);

  jnode_t* json = jfrom_string(buffer);
  free(buffer);
  if (!json) {
    println("Failed to load json: %s", jerror());
    return EXIT_FAILURE;
  }

  jnode_t* copy = jcopy(json);
  if (!copy) {
    println("Failed to copy json: %s", jerror());
    jdelete(json);
    return EXIT_FAILURE;
  }
  jdelete(json);

  char* str1 = jto_string(copy);
  println("==== JSON COPIED ====");
  println("[%s] %s", json_path, str1);
  free(str1);

  jnode_t* choices = jcopy(jobject_get(copy, "choices"));
  jdelete(copy);

  char* str2 = jto_string(choices);
  println("==== JSON CHOICES ====");
  println("[%s] %s", json_path, str2);
  free(str2);

  jdelete(choices);
  return 0;
}
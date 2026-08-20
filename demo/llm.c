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
  fclose(jp);
  buffer[len] = '\0';

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

  jnode_t* choices = jobject_get(json, "choices");
  if (!choices) {
    println("Failed to get 'choices' from json");
    jdelete(json);
    return EXIT_FAILURE;
  }

  jnode_t* choice = jarray_get(choices, 0);
  if (!choice) {
    println("Failed to get 'message' from choices");
    jdelete(json);
    return EXIT_FAILURE;
  }

  jnode_t* message = jobject_get(choice, "message");
  if (!message) {
    println("Failed to get 'message' from choice");
    jdelete(json);
    return EXIT_FAILURE;
  }

  jnode_t* content = jobject_get(message, "content");
  if (!content) {
    println("Failed to get 'content' from message");
    jdelete(json);
    return EXIT_FAILURE;
  }

  println("==== JSON CONTENT ====");
  const char* str2 = jstring_content(content);
  println("[%s] %s", json_path, str2);

  jdelete(json);
  return 0;
}
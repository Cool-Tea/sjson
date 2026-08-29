#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

#define println(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

void print_kv(const char* key, jnode_t* value, void* userp) {
  (void)userp;
  static int count = 0;
  char* jstr = jto_string(value);
  println("#%4d [%s] = %s", ++count, key, jstr);
  free(jstr);
}

int main() {
  const char* json_path = "demo/obj_iter.json";

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

  jnode_t* json = jfrom_string(buffer, 0);
  free(buffer);
  if (!json) {
    println("Failed to load json");
    return EXIT_FAILURE;
  }

  {
    jnode_t* web_app = jobject_get(json, "web-app");
    {
      jnode_t* servlet = jobject_get(web_app, "servlet");
      {
        jnode_t* item = jarray_get(servlet, 0);
        {
          jnode_t* init_param = jobject_get(item, "init-param");
          jobject_foreach(init_param, print_kv, 0);
        }
      }
    }
  }

  jdelete(json);

  return 0;
}
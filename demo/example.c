#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

int main() {
  // Create a JSON object
  jnode_t* root = jobject_new();

  // Add values
  jobject_put(root, "name", jstring_new(0, "John Doe"));
  jobject_put(root, "age", jnumber_new(30));
  jobject_put(root, "active", jbool_new(1));

  // Convert to string
  char* json_str = jto_string(root);
  printf("JSON: %s\n", json_str);

  // Parse from string
  jnode_t* parsed = jfrom_string(json_str);

  // Access values
  if (jis_object(parsed)) {
    jnode_t* name = jobject_get(parsed, "name");
    if (jis_string(name)) {
      printf("Name: %s\n", jstring_content(name));
    }
  }

  // Cleanup
  free(json_str);
  jdelete(root);
  jdelete(parsed);

  return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <sjson.h>

#define println(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

void fail_from_string() {
  println("\n=== TESTING FROM_STRING ERROR OPERATIONS ===");

  const char* early_eof = "{ \"name\": \"John\" ";
  jnode_t* n1 = jfrom_string(early_eof);
  println("TESTING %s:", early_eof);
  println("[%p] %s", n1, jerror());

  // Test invalid JSON with missing quotes
  const char* missing_quotes = "{ name: John }";
  jnode_t* n2 = jfrom_string(missing_quotes);
  println("\nTESTING %s:", missing_quotes);
  println("[%p] %s", n2, jerror());

  // Test invalid JSON with trailing comma
  const char* trailing_comma = "{ \"name\": \"John\", }";
  jnode_t* n3 = jfrom_string(trailing_comma);
  println("\nTESTING %s:", trailing_comma);
  println("[%p] %s", n3, jerror());

  // Test invalid JSON with unescaped string
  const char* unescaped = "{ \"name\": \"John\nDoe\" }";
  jnode_t* n4 = jfrom_string(unescaped);
  println("\nTESTING %s:", unescaped);
  println("[%p] %s", n4, jerror());

  // Test completely malformed JSON
  const char* malformed = "{{{";
  jnode_t* n5 = jfrom_string(malformed);
  println("\nTESTING %s:", malformed);
  println("[%p] %s", n5, jerror());
}

void fail_array_operation() {
  println("\n=== TESTING ARRAY ERROR OPERATIONS ===");

  // Test out of bounds access
  jnode_t* array = jarray_new();
  jarray_add(array, jstring_new(0, "item1"));
  jarray_add(array, jstring_new(0, "item2"));

  jnode_t* out_of_bounds = jarray_get(array, 10);
  println("Getting index 10 from 2-element array: [%p] %s", out_of_bounds,
          jerror());

  // Test Non-array access
  jnode_t* number = jnumber_new(3.14);
  int result2 = jarray_size(number);
  println("Accessing non-array: result=%d, error=%s", result2, jerror());

  // Test removing from out of bounds index
  int result3 = jarray_remove(array, -1);
  println("Removing index -1: result=%d, error=%s", result3, jerror());

  int result4 = jarray_remove(array, 100);
  println("Removing index 100: result=%d, error=%s", result4, jerror());

  // Test inserting at invalid index
  int result5 = jarray_insert(array, -5, number);
  println("Inserting at index -5: result=%d, error=%s", result5, jerror());

  // Clean up
  jdelete(number);
  jdelete(array);
}

void fail_string_operation() {
  println("\n=== TESTING STRING ERROR OPERATIONS ===");

  // Test Non-string access
  jnode_t* number = jnumber_new(3.14);
  int result2 = jstring_len(number);
  println("Accessing non-string: result=%d, error=%s", result2, jerror());

  // Test out of bounds operations
  jnode_t* string = jstring_new(0, "hello");
  char c = jstring_get(string, 100);
  println("Getting char at index 100 from 5-char string: '%c' (%d), error=%s",
          c, (int)c, jerror());

  int result3 = jstring_insert(string, -10, 'X');
  println("Inserting at index -10: result=%d, error=%s", result3, jerror());

  int result4 = jstring_remove(string, 50);
  println("Removing at index 50: result=%d, error=%s", result4, jerror());

  // Test truncating to invalid length
  int result5 = jstring_truncate(string, -5);
  println("Truncating to length -5: result=%d, error=%s", result5, jerror());

  // Clean up
  jdelete(number);
  jdelete(string);
}

void fail_object_operation() {
  println("\n=== TESTING OBJECT ERROR OPERATIONS ===");

  // Test Non-object access
  jnode_t* number = jnumber_new(3.14);
  int result1 = jobject_has(number, "invalid");
  println("Accessing non-object: result=%d, error=%s", result1, jerror());

  // Test with NULL key
  jnode_t* object = jobject_new();
  jnode_t* string = jstring_new(0, "value");
  int result2 = jobject_put(object, NULL, string);
  println("Adding with NULL key: result=%d, error=%s", result2, jerror());

  // Test getting non-existent key
  jnode_t* missing = jobject_get(object, "nonexistent");
  println("Getting non-existent key: [%p] %s", missing, jerror());

  // Test checking non-existent key
  int has_key = jobject_has(object, "missing");
  println("Checking for missing key: result=%d, error=%s", has_key, jerror());

  // Clean up
  jdelete(number);
  jdelete(string);
  jdelete(object);
}

int main() {
  println("=== SJSON ERROR HANDLING TEST SUITE ===");
  fail_from_string();
  fail_array_operation();
  fail_string_operation();
  fail_object_operation();
  println("\n=== TEST SUITE COMPLETED ===");
  return 0;
}
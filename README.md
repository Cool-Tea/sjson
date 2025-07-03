# sjson

A simple JSON library written in C, designed to be lighter than cJSON while providing a straightforward API for JSON manipulation.

## Features

- **Lightweight**: Core functionality in just two files (`sjson.h` and `sjson.c`)
- **Easy Integration**: Simply copy the core files into your project
- **Simple API**: Intuitive functions for common JSON operations
- **Type Safety**: Built-in type checking and casting macros

## Quick Start

### Integration

To use sjson in your project, simply copy `sjson.h` and `sjson.c` into your source directory and include the header:

```c
#include "sjson.h"
```

### Basic Usage

```c
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
            printf("Name: %s\n", jas_string(name)->string.data);
        }
    }
    
    // Cleanup
    free(json_str);
    jdelete(root);
    jdelete(parsed);
    
    return 0;
}
```

## API Reference

### Core Functions

#### Loading and Saving
- `char* jto_string(jnode_t* jnode)` - Convert JSON node to string (must be freed manually)
- `jnode_t* jfrom_string(const char* json_str)` - Parse JSON string into node

#### Node Creation
- `jnode_t* jnull_new()` - Create null node (singleton)
- `jnode_t* jbool_new(int value)` - Create boolean node (singleton)
- `jnode_t* jnumber_new(double value)` - Create number node
- `jnode_t* jstring_new(int len, const char* string)` - Create string node (len=0 auto-calculates)
- `jnode_t* jarray_new()` - Create array node
- `jnode_t* jobject_new()` - Create object node

#### Memory Management
- `void jdelete(jnode_t* jnode)` - Free JSON node and all children

### Type Checking Macros

```c
jis_null(node)      // Check if node is null
jis_boolean(node)   // Check if node is boolean
jis_number(node)    // Check if node is number
jas_string(node)    // Check if node is string
jis_array(node)     // Check if node is array
jis_object(node)    // Check if node is object
```

### Type Casting Macros

```c
jas_node(node)      // Cast to jnode_t*
jas_null(node)      // Cast to jnull_t*
jas_bool(node)      // Cast to jbool_t*
jas_number(node)    // Cast to jnumber_t*
jas_string(node)    // Cast to jstring_t*
jas_array(node)     // Cast to jarray_t*
jas_object(node)    // Cast to jobject_t*
```

### String Operations

```c
int jstring_len(jnode_t* jnode)                              // Get string length
char jstring_get(jnode_t* jnode, int index)                  // Get character at index
const char* jstring_content(jnode_t* jnode)                  // Get string content
int jstring_add(jnode_t* jnode, char c)                      // Add character to end
int jstring_insert(jnode_t* jnode, int index, char c)        // Insert character at index
int jstring_concat(jnode_t* jnode, const char* string)       // Concatenate string
int jstring_pop(jnode_t* jnode)                              // Remove last character
int jstring_remove(jnode_t* jnode, int index)                // Remove character at index
int jstring_truncate(jnode_t* jnode, int len)                // Retain string of length `len`
```

### Array Operations

```c
int jarray_size(jnode_t* jnode)                               // Get array size
jnode_t* jarray_get(jnode_t* jnode, int index)                // Get element at index
int jarray_add(jnode_t* jnode, jnode_t* value)                // Add element to end
int jarray_insert(jnode_t* jnode, int index, jnode_t* value)  // Insert at index
int jarray_pop(jnode_t* jnode)                                // Remove last element
int jarray_remove(jnode_t* jnode, int index)                  // Remove element at index
```

### Object Operations

```c
int jobject_size(jnode_t* jnode)                                  // Get object size
int jobject_has(jnode_t* jnode, const char* key)                  // Check if key exists
jnode_t* jobject_get(jnode_t* jnode, const char* key)             // Get value by key
int jobject_put(jnode_t* jnode, const char* key, jnode_t* value)  // Set key-value pair
```

## Working with Different Types

### Accessing Values

```c
// String
const char* str_content = jstring_content(node);
int str_len = jstring_len(node);
char first_char = jstring_get(node, 0);

// Number
jnumber_t* num_node = jas_number(node);
double num_value = num_node->value;

// Boolean
jbool_t* bool_node = jas_bool(node);
int bool_value = bool_node->value;  // 0 or 1
```

### Creating Complex Structures

```c
// Create an array of mixed types
jnode_t* array = jarray_new();
jarray_add(array, jstring_new(0, "hello"));
jarray_add(array, jnumber_new(42));
jarray_add(array, jbool_new(1));

// Create nested objects
jnode_t* person = jobject_new();
jobject_put(person, "name", jstring_new(0, "Alice"));
jobject_put(person, "hobbies", array);

jnode_t* root = jobject_new();
jobject_put(root, "person", person);
```

### String Manipulation Examples

```c
// Create a string and manipulate it
jnode_t* str = jstring_new(0, "Hello");

// Add characters
jstring_add(str, ' ');
jstring_add(str, 'W');
jstring_add(str, 'o');
jstring_add(str, 'r');
jstring_add(str, 'l');
jstring_add(str, 'd');

// Insert character at specific position
jstring_insert(str, 5, ',');  // "Hello, World"

// Concatenate another string
jstring_concat(str, "!");     // "Hello, World!"

// Get string properties
int len = jstring_len(str);                    // 13
char first = jstring_get(str, 0);              // 'H'
const char* content = jstring_content(str);    // "Hello, World!"

// Modify string
jstring_remove(str, 5);       // Remove comma: "Hello World!"
jstring_truncate(str, 5);     // Keep only first 5 chars: "Hello"
jstring_pop(str);             // Remove last char: "Hell"
```

## Error Handling

Most functions return `NULL` or `0` on error. Always check return values:

```c
jnode_t* parsed = jfrom_string(json_string);
if (parsed == NULL) {
    fprintf(stderr, "Failed to parse JSON\n");
    return -1;
}
```

## Memory Management

- Always call `jdelete()` on root nodes to free memory
- Strings returned by `jto_string()` must be freed with `free()`
- Child nodes are automatically freed when parent is deleted
- Null and boolean nodes are singletons and don't need explicit freeing, though `jdelete()` still works

## License

This project is provided as-is. Please check the license file for details.

## Contributing

Contributions are welcome! Please ensure your code follows the existing style and includes appropriate tests.
DEMO_DIR  = demo
SRC_DIR   = src
BUILD_DIR = build

DEMO_SRC  = $(wildcard $(DEMO_DIR)/*.c)
BIN 			= $(patsubst $(DEMO_DIR)/%.c,$(BUILD_DIR)/%,$(DEMO_SRC))

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(DEMO_DIR)/%.c $(SRC_DIR)/sjson.c $(SRC_DIR)/sjson.h
	@$(CC) -I$(SRC_DIR) $< $(SRC_DIR)/sjson.c -o $@ -ggdb -std=c17 -fsanitize=leak

demo: $(BUILD_DIR) $(BIN)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: demo clean
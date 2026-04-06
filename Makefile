CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lm
TARGET  = server
SRC     = server.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "✅  Build successful!"
	@echo "    Run: ./server"
	@echo "    Then open frontend/index.html in your browser"
	@echo ""

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)

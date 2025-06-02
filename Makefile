CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
CFLAGS += -I.
LDFLAGS = -lrt -lcurl -lm #

SRCS = main.c utils.c irc_core.c irc_network.c child_processes.c gemini_integration.c cJSON.c
OBJS = $(SRCS:.c=.o)
TARGET = irc_chatbot

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(filter-out cJSON.o,$(OBJS)) cJSON.o -o $(TARGET) $(LDFLAGS)

%.o: %.c irc_bot.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
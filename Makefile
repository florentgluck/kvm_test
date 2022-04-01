OUTPUT=kvm_test

CC=gcc -std=gnu11 -Wall -Wextra -MMD -g -O3

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
DEPS=$(OBJS:%.o=%.d)

$(OUTPUT): $(OBJS)
	$(CC) $^ -o $@

%.o: %.c
	$(CC) -c $< -o $@

run: $(OUTPUT)
	./$<

clean:
	rm -f $(OBJS) $(DEPS) $(OUTPUT)

.PHONY: clean

-include $(DEPS)

src = $(wildcard ../src/*.c)  
obj = $(patsubst ../src/%.c, ./%.o, $(src)) 

CC = gcc
myArgs = -Wall
target = test libxscjson.a libxscjson.so

ALL:$(target)

test:$(obj)
	$(CC) $^ -o $@ $(myArgs)

libxscjson.a:xscjson.o
	ar rs libxscjson.a xscjson.o

libxscjson.so:xscjson.o
	$(CC) -shared -o libxscjson.so xscjson.o $(myArgs)

$(obj):%.o:../src/%.c
	$(CC) -c $^ -o $@ $(myArgs) -fPIC

clean:
	-rm -rf $(obj) $(target)

.PHONY: clean ALL
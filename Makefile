
PROJECT := insaned

CFLAGS := -Wall -Wextra -pedantic -pipe -O2 -std=c99 -Isrc


all : $(PROJECT)

$(PROJECT) : src/insaned.o
	gcc $(CFLAGS) $^ -o $@ -lsane

%.o : %.c %.h
	gcc $(CFLAGS) -c $< -o $@


.PHONY : clean

clean :
	rm -rf src/insaned.o $(PROJECT)


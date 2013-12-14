
PROJECT := insaned

CFLAGS := -Wall -Wextra -pedantic -pipe -O2 -std=c99


all : $(PROJECT)

$(PROJECT) : scanimage.o
	gcc $(CFLAGS) $^ -o $@ -lsane

%.o : %.c %.h
	gcc $(CFLAGS) -c $< -o $@


.PHONY : clean

clean :
	rm -rf scanimage.o $(PROJECT)


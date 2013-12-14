
PROJECT := insaned

CFLAGS := -Wall -Wextra -pedantic -pipe -O2


all : $(PROJECT)

$(PROJECT) : scanimage.o
	gcc $(CFLAGS) $^ -o $@

%.o : %.c %.h
	gcc $(CFLAGS) -c $< -o $@


.PHONY : clean

clean :
	rm -rf scanimage.o $(PROJECT)


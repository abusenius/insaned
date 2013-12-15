
PROJECT := insaned

CFLAGS := -Wall -Wextra -pedantic -pipe -O2 -std=c++11 -Isrc


all : $(PROJECT)

$(PROJECT) : src/insaned.o
	g++ $(CFLAGS) $^ -o $@ -lsane

%.o : %.cpp %.h
	g++ $(CFLAGS) -c $< -o $@


.PHONY : clean

clean :
	rm -rf src/insaned.o $(PROJECT)


CC = gcc
CFLAGS = -g -Wall
OPTFLAGS = -O3
LDFLAGS = -lm

SRC = src
OBJ = obj
EXECUTABLES = sobel
SOURCES = $(wildcard $(SRC)/*.c)
OBJECTS = $(patsubst $(SRC)/%.c,$(OBJ)/%.o,$(SOURCES))

sobel: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c $< $(LDFLAGS) -o $@ 

clean:
	rm -f $(EXECUTABLES) *.jpg output_sobel.grey

# make image will create the output_sobel.jpg from the output_sobel.grey. 
# Remember to change this rule if you change the name of the output file.
image: output_sobel.grey
	convert -depth 8 -size 1024x1024 GRAY:output_sobel.grey output_sobel.jpg 


VMEM_PAGESIZE=8
	
CC = gcc
CFLAGS = -g -DVMEM_PAGESIZE=$(VMEM_PAGESIZE)
LDFLAGS = -g -DVMEM_PAGESIZE=$(VMEM_PAGESIZE) -lpthread

SRC1 = mmanage.c pagefile.c logger.c
SRC2 = vmaccess.c vmappl.c
SRC = $(SRC1) $(SRC2)
OBJ1 = $(SRC1:%.c=%.o)
OBJ2 = $(SRC2:%.c=%.o)

all: mmanage vmappl

mmanage: $(OBJ1)
	$(CC) -o mmanage $(OBJ1) $(LDFLAGS)
vmappl: $(OBJ2)
	 $(CC) -o vmappl $(OBJ2) $(LDFLAGS)


.PHONY: clean
clean:
	rm -rf $(OBJ1)
	rm -rf $(OBJ2)
	rm -rf mmanage vmappl
	rm -rf logfile.txt pagefile.bin

.PHONY: deps
deps: 	
	$(CC) $(CFLAGS) $(SRC) > makefile.dependencies

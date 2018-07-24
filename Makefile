
PROGS= server

all: $(PROGS)

clean:
	rm -f *.o core $(PROGS)

cc = cc
CC = CC

COPT    = -g2 -O2

DMAPP_PATH=/opt/cray/dmapp/default
GNIH_PATH=/opt/cray/gni-headers/default
UGNI_PATH=/opt/cray/ugni/default
UDREG_PATH=/opt/cray/udreg/default
PMI_PATH=/opt/cray/pmi/default

INCLUDE = -I$(DMAPP_PATH)/include -I$(UDREG_PATH)/include -I$(GNIH_PATH)/include -I$(PMI_PATH)/include
LIBS    = -L$(DMAPP_PATH)/lib64   -L$(UDREG_PATH)/lib64   -L$(UGNI_PATH)/lib64   -L$(PMI_PATH)/lib64 -ldmapp -ludreg -lugni -lpmi

CFLAGS  = $(COPT) $(INCLUDE)

LD      = $(CC)
LDFLAGS = $(COPT)

OBJ :=	node.o
all: ${OBJ} test.x

%.o: %.cc
	$(CC) $(CFLAGS) $(LIBS) -c $< -o $@

%.x: %.o
	$(LD) $(LDFLAGS) ${OBJ} $< $(LIBS) -o $@

clean:
	$(RM) $(RMFLAGS) $(OBJ)

realclean: clean
	$(RM) $(RMFLAGS) $(TESTS)
	$(RM) $(RMFLAGS) $(ASM)

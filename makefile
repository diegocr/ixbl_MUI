PROG	=	ixbl_MUI
CC	=	m68k-amigaos-gcc
WARNS	=	-W -Wall #-Winline
OPTIM	=	-m68020-60 -msoft-float -O2 -msmall-code -fomit-frame-pointer
DEFS	=	#-DDEBUG -g
CFLAGS	=	-noixemul $(OPTIM) $(WARNS) $(DEFS)
LIBS	=	-lmui -lamiga -Wl,-Map,$@.map,--cref
LINK	=	$(CC) -nostdlib $(OPTIM) -s
OBJS	=	$(PROG).o ixblogo.o

all:	$(PROG)
$(PROG):	$(OBJS)
	$(LINK) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	@echo Compiling $@
	@$(CC) $(CFLAGS) -c $< -o $@

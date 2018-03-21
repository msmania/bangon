!IF "$(PLATFORM)"=="X64" || "$(PLATFORM)"=="x64"
ARCH=amd64
!ELSE
ARCH=x86
!ENDIF

OUTDIR=bin\$(ARCH)
OBJDIR=obj\$(ARCH)
SRCDIR=src

TARGET=on.dll
CC=cl
LINKER=link
RD=rd /s /q
RM=del /q
DEF=$(SRCDIR)\bangon.def

OBJS=\
	$(OBJDIR)\dllmain.obj\
	$(OBJDIR)\peimage.obj\
	$(OBJDIR)\dt.obj\

LIBS=\

# warning C4100: unreferenced formal parameter
CFLAGS=\
	/nologo\
	/Zi\
	/c\
	/Fo"$(OBJDIR)\\"\
	/Fd"$(OBJDIR)\\"\
	/DUNICODE\
	/O2\
	/EHsc\
	/W4\
	/wd4100\

LFLAGS=\
	/NOLOGO\
	/DEBUG\
	/SUBSYSTEM:WINDOWS\
	/DLL\
	/DEF:$(DEF)\
	/INCREMENTAL:NO\

all: $(OUTDIR)\$(TARGET)

$(OUTDIR)\$(TARGET): $(OBJS)
	@if not exist $(OUTDIR) mkdir $(OUTDIR)
	$(LINKER) $(LFLAGS) $(LIBS) /PDB:"$(@R).pdb" /OUT:"$@" $**

{$(SRCDIR)}.cpp{$(OBJDIR)}.obj:
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	$(CC) $(CFLAGS) $<

clean:
	@if exist $(OBJDIR) $(RD) $(OBJDIR)
	@if exist $(OUTDIR)\$(TARGET) $(RM) $(OUTDIR)\$(TARGET)
	@if exist $(OUTDIR)\$(TARGET:dll=pdb) $(RM) $(OUTDIR)\$(TARGET:dll=pdb)
	@if exist $(OUTDIR)\$(TARGET:dll=exp) $(RM) $(OUTDIR)\$(TARGET:dll=exp)

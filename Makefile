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
	$(OBJDIR)\common.obj\
	$(OBJDIR)\dllmain.obj\
	$(OBJDIR)\dt.obj\
	$(OBJDIR)\kd.obj\
	$(OBJDIR)\peimage.obj\
	$(OBJDIR)\process.obj\
	$(OBJDIR)\symbol_manager.obj\
	$(OBJDIR)\thread.obj\
	$(OBJDIR)\utils.obj\
	$(OBJDIR)\vtable_manager.obj\

LIBS=\
	dbgeng.lib\

# warning C4100: unreferenced formal parameter
# warning C4201: nonstandard extension used: nameless struct/union
CFLAGS=\
	/nologo\
	/Zi\
	/c\
	/Fo"$(OBJDIR)\\"\
	/Fd"$(OBJDIR)\\"\
	/DUNICODE\
	/D_CRT_SECURE_NO_WARNINGS\
	/Od\
	/EHsc\
	/W4\
	/wd4100\
	/wd4201\

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

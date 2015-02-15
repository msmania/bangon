!IF "$(PLATFORM)"=="X64"
OUTDIR=.\bin64
!ELSE
OUTDIR=.\bin
!ENDIF

CC=cl
LINKER=link
RM=del /q

TARGET=on.dll
DEF=bangon.def
OBJS=\
    $(OUTDIR)\dllmain.obj\
    $(OUTDIR)\bangon.obj\
    $(OUTDIR)\peimage.obj\
    $(OUTDIR)\dt.obj

# warning C4100: 'hCurrentProcess' : unreferenced formal parameter
CFLAGS=\
    /nologo\
    /Zi\
    /c\
    /Fo"$(OUTDIR)\\"\
    /Fd"$(OUTDIR)\\"\
    /D_UNICODE\
    /DUNICODE\
    /O2\
    /EHsc\
    /W4\
    /wd 4100

LFLAGS=\
    /NOLOGO\
    /DEBUG\
    /SUBSYSTEM:WINDOWS\
    /DLL\
    /DEF:$(DEF)

all: clean $(OUTDIR)\$(TARGET)

clean:
    -@if not exist $(OUTDIR) md $(OUTDIR)
    @$(RM) /Q $(OUTDIR)\* 2>nul

$(OUTDIR)\$(TARGET): $(OBJS)
    $(LINKER) $(LFLAGS) /PDB:"$(@R).pdb" /OUT:"$(OUTDIR)\$(TARGET)" $**

.cpp{$(OUTDIR)}.obj:
    $(CC) $(CFLAGS) $<

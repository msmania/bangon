// https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64

typedef enum _UNWIND_OP_CODES {
  UWOP_PUSH_NONVOL = 0, /* info == register number */
  UWOP_ALLOC_LARGE,     /* no info, alloc size in next 2 slots */
  UWOP_ALLOC_SMALL,     /* info == size of allocation / 8 - 1 */
  UWOP_SET_FPREG,       /* no info, FP = RSP + UNWIND_INFO.FPRegOffset*16 */
  UWOP_SAVE_NONVOL,     /* info == register number, offset in next slot */
  UWOP_SAVE_NONVOL_FAR, /* info == register number, offset in next 2 slots */
  UWOP_SAVE_XMM128 = 8, /* info == XMM reg number, offset in next slot */
  UWOP_SAVE_XMM128_FAR, /* info == XMM reg number, offset in next 2 slots */
  UWOP_PUSH_MACHFRAME   /* info == 0: no error-code, 1: error-code */
} UNWIND_CODE_OPS;

typedef union _UNWIND_CODE {
  struct {
    uint8_t CodeOffset;
    uint8_t UnwindOp : 4;
    uint8_t OpInfo   : 4;
  };
  USHORT FrameOffset;
} UNWIND_CODE, *PUNWIND_CODE;

#ifndef UNW_FLAG_NHANDLER
#define UNW_FLAG_EHANDLER  0x01
#endif
#ifndef UNW_FLAG_UHANDLER
#define UNW_FLAG_UHANDLER  0x02
#endif
#ifndef UNW_FLAG_CHAININFO
#define UNW_FLAG_CHAININFO 0x04
#endif

typedef struct _UNWIND_INFO {
  uint8_t Version       : 3;
  uint8_t Flags         : 5;
  uint8_t SizeOfProlog;
  uint8_t CountOfCodes;
  uint8_t FrameRegister : 4;
  uint8_t FrameOffset   : 4;
  UNWIND_CODE UnwindCode[1];
/*
  UNWIND_CODE MoreUnwindCode[((CountOfCodes + 1) & ~1) - 1];
  union {
    OPTIONAL uint32_t ExceptionHandler;
    OPTIONAL uint32_t FunctionEntry;
  };
  OPTIONAL uint32_t ExceptionData[];
*/
} UNWIND_INFO, *PUNWIND_INFO;

struct RUNTIME_FUNCTION_AMD64 {
  uint32_t BeginAddress;
  uint32_t EndAddress;
  uint32_t UnwindData;
};

#define GetUnwindCodeEntry(info, index) \
    ((info)->UnwindCode[index])

#define GetLanguageSpecificDataPtr(info) \
    ((PVOID)&GetUnwindCodeEntry((info),((info)->CountOfCodes + 1) & ~1))

#define GetExceptionHandler(base, info) \
    ((PEXCEPTION_HANDLER)((base) + *(PULONG)GetLanguageSpecificDataPtr(info)))

#define GetChainedFunctionEntry(base, info) \
    ((PRUNTIME_FUNCTION)((base) + *(PULONG)GetLanguageSpecificDataPtr(info)))

#define GetExceptionDataPtr(info) \
    ((PVOID)((PULONG)GetLanguageSpecificData(info) + 1)

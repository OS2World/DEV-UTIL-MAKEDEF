//***** objutils.h  --  Global include info for Object Library Engine (objutils.c) ******

#define THEADR              0x80        // OMF module header
#define COMENT              0x88        // OMF comment record
#define MODEND              0x8A        // OMF module end record
#define LIBMOD              0xA3        // library module name comment class
#define LIBHEADER           0xF0        // LIB file header
#define MARKER_RECORD       0xF1        // marker between modules & dictionary
#define NUMBUCKETS          37          // number of buckets/block    
#define DICTBLOCKSIZE       512         // bytes/symbol dictionary block
#define DICTBLKFULL         0xFF        // Symbol dictionary block full

#define UNDEFINED           -1          // to indicate non-initialized data
#define STR_EQUAL           0           // string equality

// These two macros will rotate word operand opw by nbits bits (0 - 16) 
#define WORDBITS            16
#define ROL(opw, nbits) (((opw) << (nbits)) | ((opw) >> (WORDBITS - (nbits))))
#define ROR(opw, nbits) (((opw) >> (nbits)) | ((opw) << (WORDBITS - (nbits))))

typedef enum {
    false,
    true
} bool;

#pragma pack(1)

typedef struct {
    unsigned char RecType;
    int RecLength;
} OMFHEADER;

typedef struct {
    unsigned char RecType;
    int RecLength;
    unsigned char Attrib;
    unsigned char CommentClass;
} COMENTHEADER;

typedef struct {                    // Record Type F0h
    int PageSize;                   // Header length (excl. first 3 bytes)
                                    // == page size (module at page boundary)
                                    // page size == 2 ** n, 4 <= n <= 15
    long DictionaryOffset;          // file offset of Symbol Dictionary
    int NumDictBlocks;              // number of Symbol Dictionary blocks
                                    // <= 251 512-byte dictionary pages
    unsigned char Flags;            // only valid flag: 01h => case-sensitive
    bool IsCaseSensitive;
    bool IsLIBMODFormat;            // is MS extension type LIBMOD present?
} LIBHDR;

typedef struct {
    unsigned char MarkerType;       // This's better be F1h
    int MarkerLength;               // filler to dictionary's 512-byte alignment
} DICTMARKER;

typedef struct {
    int  BlockNumber;
    int  BucketNumber;
    unsigned char *SymbolP;
    long ModuleFilePos;
    bool IsFound;
} DICTENTRY;

typedef struct {
    int BlockHash;
    int BlockOvfl;
    int BucketHash;
    int BucketOvfl;
} HashT;

void GetLibHeader(LIBHDR *LibHeader, FILE *InLibFH);
HashT Hash(char SymbolZ[], int NumHashBlocks);
DICTENTRY FindSymbol(char *SymbolZ, LIBHDR *LibHeader, FILE *InLibFH);
void GetSymDictBlock(int BlockNumber, LIBHDR *LibHeader, 
        FILE *InLibFH);
long FindModule(char *ModuleName, LIBHDR *LibHeader, FILE *InLibFH);
DICTENTRY GetSymDictEntry(int BlockNumber, int BucketNumber, 
        LIBHDR *LibHeader, FILE *InLibFH);
char *GetModuleName(long ModuleFilePos, LIBHDR *LibHeader, FILE *InLibFH);
bool FindLIBMOD(FILE *InLibFH);
bool FindObjRecord(FILE *ObjFH, unsigned char RecType);
bool ExtractModule(char *ModuleName, char *NewModuleName, LIBHDR *LibHeader, 
        FILE *InLibFH);
void CopyObjModule(FILE *NewObjFH, long FilePos, FILE *InLibFH);

//******  --  Service functions *******

#define NOFILE              NULL        // no error log file
typedef enum {
    Message,
    Warning,
    Error
} MESSAGETYPE;

char *MakeASCIIZ(unsigned char *LString);
void Output(MESSAGETYPE MsgType, FILE *Stream, char *OutputFormat, ...);

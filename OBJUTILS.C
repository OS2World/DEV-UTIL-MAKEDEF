//***** objutils.c  --  Object Library Engine ******

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include "objutils.h"

typedef struct
{
    unsigned char SymbolDictBlock[DICTBLOCKSIZE]; // symbol dictionary block
    int FreeSpaceIdx;                // cursor to next free symbol space slot
    bool IsFull;                     // is this sym. dict. block full?
    int BlockNumber;                 // current block number
} DICTBLOCK;

// The number of pages in the Symbol Dictionary has to be a prime <= 251.
// NOTE: The smallest page number in MS LIB is 2, in Borland TLIB it's 1.

static int Primes[] =
{
    2,   3,   5,   7,  11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,
   53,  59,  61,  67,  71,  73,  79,  83,  89,  97, 101, 103, 107, 109, 113,
  127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197,
  199, 211, 223, 227, 229, 233, 239, 241, 251
};

// Symbol Dictionary Block
static DICTBLOCK DictBlock;

//  GetLibHeader -- Get header of an object module library. The library 
//  header's ( record type F0) main purpose is to identify this data file as a
//  library, give page size, and size and location of Symbol Dictionary.

void GetLibHeader(LIBHDR *LibHeader, FILE *InLibFH)
{
    if (fgetc(InLibFH) != LIBHEADER) 
       Output(Error, NOFILE, "Bogus Library Header\n");

    // NOTE: The LIBHDR data structure has been enlarged to include more
    // info than the actual LIB header contains.  As a result, a few more bytes
    // are read in past the actual header when we take sizeof(LIBHDR).  This
    // is no problem since there's plenty to read after the header, anyway!

    if (fread(LibHeader, sizeof(LIBHDR), 1, InLibFH) != 1) 
       Output(Error, NOFILE, "Couldn't Read Library Header\n");

    // Add in Header length word & checksum byte
    LibHeader->PageSize += 3;

    // Determine if LIB includes Microsoft's LIBMOD extension
    // Find the first OBJ module in the LIB file
    if (fseek(InLibFH, (long) LibHeader->PageSize, SEEK_SET) != 0)
       Output(Error, NOFILE, "Seek for first object module failed\n");

    LibHeader->IsLIBMODFormat = FindLIBMOD(InLibFH);

    LibHeader->IsCaseSensitive = LibHeader->Flags == 0x01 ? true : false;

    // Make it clear that we haven't read Symbol Dictionary yet
    DictBlock.BlockNumber = UNDEFINED;
}

//  FindModule -- Find a module in Symbol Dictionary and return its file
//  position.  If not found, return -1L.

long FindModule(char *ModuleName, LIBHDR *LibHeader, FILE *InLibFH)
{
    char *ObjName;
    DICTENTRY DictEntry;
    char *ExtP;

    // Allow extra space for terminating "!\0"
    if ((ObjName = malloc(strlen(ModuleName) + 2)) == NULL) 
       Output(Error, NOFILE, "OBJ Name Memory Allocation Failed\n");

    strcpy(ObjName, ModuleName);

    // Allow search for module name xxx.obj
    if ((ExtP = strrchr(ObjName, '.')) != NULL)
       *ExtP = '\0';

    // NOTE: Module names are stored in LIB's with terminating '!'
    strcat(ObjName, "!");

    DictEntry = FindSymbol(ObjName, LibHeader, InLibFH);

    free(ObjName);

    return (DictEntry.IsFound == true ? DictEntry.ModuleFilePos : -1L);
}

//  FindSymbol  --  Find a symbol in Symbol Dictionary by (repeatedly, if 
//  necessary) hashing the symbol and doing dictionary lookup.

DICTENTRY FindSymbol(char *SymbolZ, LIBHDR *LibHeader, FILE *InLibFH)
{
    DICTENTRY DictEntry;
    char *SymbolP;
    HashT HashVal;
    int MaxTries;
    int Block, Bucket;

    HashVal = Hash(SymbolZ, LibHeader->NumDictBlocks);
    Block = HashVal.BlockHash;
    Bucket = HashVal.BucketHash;
    MaxTries = LibHeader->NumDictBlocks * NUMBUCKETS;
    DictEntry.IsFound = false;

    while (MaxTries--)
       {
       DictEntry = GetSymDictEntry(Block, Bucket, LibHeader, InLibFH);

       // Three alternatives to check after Symbol Dictionary lookup:
       // 1. If the entry is zero, but the dictionary block is NOT full,
       //    the symbol is not present:
       if (DictEntry.IsFound == false && DictBlock.IsFull == false)
          return (DictEntry);

       // 2. If the entry is zero, and the dictionary block is full, the
       //    symbol may have been rehashed to another block; keep looking:
       // 3. If the entry is non-zero, we still have to verify the symbol.
       //    If it's the wrong one (hash clash), keep looking:
       if (DictEntry.IsFound == true)
          {
          // Get the symbol name
          SymbolP = MakeASCIIZ(DictEntry.SymbolP);

          // Choose case-sensitive or insensitive comparison as appropriate
          if ((LibHeader->IsCaseSensitive == true ? strcmp(SymbolZ, SymbolP) :
               stricmp(SymbolZ, SymbolP)) == STR_EQUAL)
             {
             free(SymbolP);
             return (DictEntry);
             }

          free(SymbolP);
          }

       // Cases 2 and 3 (w/o a symbol match) require re-hash:
       Block += HashVal.BlockOvfl;
       Bucket += HashVal.BucketOvfl;
       Block %= LibHeader->NumDictBlocks;
       Bucket %= NUMBUCKETS;
       }

    // We never found the entry!
    DictEntry.IsFound = false;

    return (DictEntry);
}

//  Hash  --  Hash a symbol for Symbol Dictionary entry
//  Inputs: SymbolZ - Symbol in ASCIIZ form; NumHashBlocks - current number of 
//    Symbol Dictionary blocks (MS LIB max. 251 blocks)
//  Outputs: Hash data structure, containing: BlockHash, index of block 
//    containing symbol; BlockOvfl, block index's rehash delta; BucketHash, 
//    index of symbol's bucket (position) on page; BucketOvfl, bucket index's 
//    rehash delta
//  Algorithm: Determine block index, i.e. page number in Symbol Dictionary 
//    where the symbol is to reside, and the bucket index, i.e. the position 
//    within that page (0-36). If this leads to collision, retry with bucket 
//    delta until entire block has turned out to be full. Then, apply block 
//    delta, and start over with original bucket index.

HashT Hash(char SymbolZ[], int NumHashBlocks)
{
    HashT SymHash;                     // the resulting aggregate hash values
    unsigned char *SymbolC;            // symbol with prepended count
    int  SymLength;                    // length of symbol to be hashed
    unsigned char *FwdP, *BwdP;        // temp. pts's to string: forward/back.
    unsigned int FwdC, BwdC;           // current char's at fwd/backw. pointers
    unsigned int BlockH, BlockD, BucketH, BucketD;   // temporary values 
    int i;

    SymLength = strlen(SymbolZ);

    // Make symbol string in Length byte/ASCII string format
    if ((SymbolC = malloc(SymLength + 2)) == NULL)
       Output(Error, NOFILE, "Memory Allocation Failed\n");

    SymbolC[0] = (unsigned char) SymLength;

    // copy w/o EOS
    strncpy((signed char *) &SymbolC[1], SymbolZ, SymLength + 1);

    FwdP = &SymbolC[0];
    BwdP = &SymbolC[SymLength];
    BlockH = BlockD = BucketH = BucketD = 0;
    for (i = 0; i < SymLength; i++)
       {
       // Hashing is done case-insensitive, incl. length byte
       FwdC = (unsigned int) *FwdP++ | 0x20;
       BwdC = (unsigned int) *BwdP-- | 0x20;

       // XOR the current character (moving forward or reverse, depending
       // on variable calculated) with the intermediate result rotated
       // by 2 bits (again, left or right, depending on variable).
       // Block Hash: traverse forward, rotate left
       BlockH = FwdC ^ ROL(BlockH, 2);

       // Block Overflow delta: traverse reverse, rotate left
       BlockD = BwdC ^ ROL(BlockD, 2);

       // Bucket Hash: traverse reverse, rotate right
       BucketH = BwdC ^ ROR(BucketH, 2);

       // Bucket Overflow delta: traverse forward, rotate right
       BucketD = FwdC ^ ROR(BucketD, 2);
       }

    // NOTE: Results are zero-based
    SymHash.BlockHash = BlockH % NumHashBlocks;
    SymHash.BucketHash = BucketH % NUMBUCKETS;

    // Obviously, hash deltas of 0 would be nonsense!
    SymHash.BlockOvfl = max(BlockD % NumHashBlocks, 1);
    SymHash.BucketOvfl = max(BucketD % NUMBUCKETS, 1);

    free(SymbolC);
    return (SymHash);
}

//  GetSymDictBlock  --  Read and pre-process a Symbol Dictionary block

void GetSymDictBlock(int BlockNumber, LIBHDR *LibHeader, FILE *InLibFH)
{
    // Find and read the whole Symbol Dictionary block
    if (fseek(InLibFH, LibHeader->DictionaryOffset + (long) BlockNumber * 
            (long) DICTBLOCKSIZE, SEEK_SET) != 0)
       Output(Error, NOFILE, "Could Not Find Symbol Dictionary\n");

    if (fread(DictBlock.SymbolDictBlock, DICTBLOCKSIZE, 1, InLibFH) != 1) 
       Output(Error, NOFILE, "Couldn't Read Library Header\n");

    // Is this block all used up?
    DictBlock.FreeSpaceIdx = DictBlock.SymbolDictBlock[NUMBUCKETS];
    DictBlock.IsFull = (DictBlock.FreeSpaceIdx == DICTBLKFULL) ? true : false;

    // For future reference, remember block number
    DictBlock.BlockNumber = BlockNumber;
}

//  GetSymDictEntry
//  Look up and process a Symbol Dictionary block entry

DICTENTRY GetSymDictEntry(int BlockNumber, int BucketNumber, LIBHDR *LibHeader, FILE *InLibFH)
{
    DICTENTRY DictEntry;
    unsigned char SymbolOffset;
    unsigned char SymbolLength;
    int PageNumber;
    // Remember entry's block/bucket and init. to no (NULL) entry
    DictEntry.BlockNumber = BlockNumber;
    DictEntry.BucketNumber = BucketNumber;
    DictEntry.SymbolP = NULL;   
    DictEntry.IsFound = false;

    // Make sure the appropriate block was already read from obj. mod. library
    if (DictBlock.BlockNumber != BlockNumber)
        GetSymDictBlock(BlockNumber, LibHeader, InLibFH);

    // WORD offset of symbol in dictionary block: 0 means no entry
    SymbolOffset = DictBlock.SymbolDictBlock[BucketNumber];

    if (SymbolOffset != 0)
       {
       // Since it's word offset, need to multiply by two
       DictEntry.SymbolP = &DictBlock.SymbolDictBlock[SymbolOffset * 2];

       // Get the symbol's object module offset in LIB
       SymbolLength = *DictEntry.SymbolP;

       // Object module's LIB page number is right after symbol string
       PageNumber = *(int *) (DictEntry.SymbolP + SymbolLength + 1);

       DictEntry.ModuleFilePos = (long) PageNumber * (long)LibHeader->PageSize;
       DictEntry.IsFound = true;
       }
    return (DictEntry);
}

//  GetModuleName -- Read the OMF module header record (THEADR - 80h) or, if
//    present, MS's LIBMOD extension record type. NOTE: For Microsoft C, 
//    THEADR reflects the source code name file at compilation time. OBJ name 
//    may differ from this; the LIBMOD record will contain its name. For 
//    Borland C++, THEADR is the only pertinent record and will contain OBJ 
//    module's name rather than the source's.

char *GetModuleName(long ModuleFilePos, LIBHDR *LibHeader, FILE *InLibFH)
{
    int SymbolLength;
    char *ModuleName;
    OMFHEADER OmfHeader;

    // Position at beginning of pertinent object module
    if (fseek(InLibFH, ModuleFilePos, SEEK_SET) != 0)
        Output(Error, NOFILE, "Seek for object module at %lx failed\n", ModuleFilePos);

    if (LibHeader->IsLIBMODFormat == false)
       {
       if (fread(&OmfHeader, sizeof(OmfHeader), 1, InLibFH) != 1)
          Output(Error, NOFILE, "Couldn't Read THEADR at %lx\n", ModuleFilePos);

       if (OmfHeader.RecType != THEADR)
          Output(Error, NOFILE, "Bogus THEADR OMF record at %lx\n", ModuleFilePos);
       }
    else
       if (FindLIBMOD(InLibFH) == false)
          {
          Output(Warning, NOFILE, "No LIBMOD record found at %lx\n", ModuleFilePos);
          return (NULL);
          }

    SymbolLength = fgetc(InLibFH);

    if ((ModuleName = malloc(SymbolLength + 1)) == NULL)
       Output(Error, NOFILE, "Malloc failure Reading module name\n");

    if (fread(ModuleName, SymbolLength, 1, InLibFH) != 1) 
       Output(Error, NOFILE, "Couldn't Read THEADR\n");

    ModuleName[SymbolLength] = '\0';

    return(ModuleName);
}

//  FindLIBMOD  --  Get a LIBMOD (A3) comment record, if present.
//  NOTE: This is a special OMF COMENT (88h) record comment class used by
//  Microsoft only.  It provides the name of the object modules which may 
//  differ from the source (contained in THEADR). This record is added when an
//  object module is put into library, and stripped out when it's extracted. 
//  This routine will leave file pointer at the LIBMOD name field.

bool FindLIBMOD(FILE *InLibFH)
{
    COMENTHEADER CommentHdr;

    // Search (up to) all COMENT records in OBJ module
    while (FindObjRecord(InLibFH, COMENT) == true)
       {
       if (fread(&CommentHdr, sizeof(CommentHdr), 1, InLibFH) != 1)
          Output(Error, NOFILE, "Couldn't Read OBJ\n");

       if (CommentHdr.CommentClass == LIBMOD)
           return (true);
       else
          // if not found: forward to next record, and retry
          if (fseek(InLibFH, (long) CommentHdr.RecLength
                                    - sizeof(CommentHdr)
                                    + sizeof(OMFHEADER), SEEK_CUR) != 0)
             Output(Error, NOFILE, "Seek retry for LIBMOD failed\n");
       }

    // We got here only if COMENT of class LIBMOD was never found
    return (false);
}

//  FindObjRecord  --  Find an object module record in one given module.
//  On call, file pointer must be set to an objec record.  Search will
//  quit at the end of current module (or when record found).

bool FindObjRecord(FILE *ObjFH, unsigned char RecType)
{
    OMFHEADER ObjHeader;

    while (fread(&ObjHeader, sizeof(ObjHeader), 1, ObjFH) == 1)
       {
       // If it's the record type we're looking for, we're done
       if (ObjHeader.RecType == RecType)
          {
          // Return with obj module set to record requested
          if (fseek(ObjFH, -(long) sizeof(ObjHeader), SEEK_CUR) != 0)
             Output(Error, NOFILE, "Seek for Record Type %02x failed\n", RecType & 0xFF);
          return (true);
          }

       // End of object module, record type NEVER found
       if (ObjHeader.RecType == MODEND)
          return (false);

       // Forward file pointer to next object module record
       if (fseek(ObjFH, (long) ObjHeader.RecLength, SEEK_CUR) != 0)
          Output(Error, NOFILE, "Seek retry for Record Type %02x failed\n", RecType & 0xFF);
       }

    // If this quit due to I/O condition, it's either EOF or I/O error
    if (feof(ObjFH) == 0) 
       Output(Error, NOFILE, "Couldn't Read OBJ\n");

    // we completed w/o error and w/o finding the record (should NEVER happen)
    return (false);
}

//  ExtractModule -- Find an object module in a library and extract it into
//  "stand-alone" object file.  Return true if ok, else false. 
//  Optional: Can specify a new name for the module.

bool ExtractModule(char *ModuleName, char *NewModuleName, LIBHDR *LibHeader, FILE *InLibFH)
{
    long FilePos;
    char *NewObjP;
    char *NewObjName;
    FILE *NewObjFH;

    // Find the object module's position in the library file
    FilePos = FindModule(ModuleName, LibHeader, InLibFH);

    if (FilePos == -1L)
        return (false);

    // Determine name for new .obj, and set it up
    NewObjP = NewModuleName != NULL ? NewModuleName : ModuleName;

    if ((NewObjName = malloc(strlen(NewObjP) + 5)) == NULL)
       Output(Error, NOFILE, "Malloc failure Making module name %s\n", NewObjP);

    strcpy(NewObjName, NewObjP);

    // Open the new .obj file, and pass everything off to low-level routine
    if ((NewObjFH = fopen(NewObjName, "wb")) == NULL)
       Output(Error, NOFILE, "Open failure new module %s\n", NewObjName);

    CopyObjModule(NewObjFH, FilePos, InLibFH);

    fclose(NewObjFH);

    free(NewObjName);

    return (true);
}

//  CopyObjModule  --  Low-level copy of LIB member to OBJ file.

void CopyObjModule(FILE *NewObjFH, long FilePos, FILE *InLibFH)
{
    OMFHEADER RecHdr;

    // Get to the object module in LIB 
    if (fseek(InLibFH, FilePos, SEEK_SET) != 0)
       Output(Error, NOFILE, "Seek failure to file position %ld\n", FilePos);

    // Write module from LIB to separate obj file
    do {
       // Read OMF header record, this will give record type and length
       if (fread(&RecHdr, sizeof(RecHdr), 1, InLibFH) != 1)
          Output(Error, NOFILE, "Couldn't Read OBJ\n");

       // Need to check every COMENT record to make sure to strip LIBMOD out
       if (RecHdr.RecType == COMENT)
          {
          // Throw away next byte (Attrib COMENT byte) for now
          fgetc(InLibFH);

          // Check COMENT's Comment Class
          // If it's a LIBMOD, set file pointer ro next record and continue
          if (fgetc(InLibFH) == LIBMOD)
             {
             if (fseek(InLibFH, (long) RecHdr.RecLength - 2L, SEEK_CUR) != 0)
                 Output(Error, NOFILE, "Seek error on COMENT\n");
             continue;
             }
          else
             // Wasn't a LIBMOD: reset file pointer to continue normally
             if (fseek(InLibFH, -2L, SEEK_CUR) != 0)
                Output(Error, NOFILE, "Seek error on COMENT\n");
          }

       if (fwrite(&RecHdr, sizeof(RecHdr), 1, NewObjFH) != 1)
          Output(Error, NOFILE, "Couldn't Write new OBJ\n");

       while (RecHdr.RecLength--)
          fputc(fgetc(InLibFH), NewObjFH);

       } while (RecHdr.RecType != MODEND);
}


//****** --  Service functions *******

// MakeASCIIZ - Take a string of 1-byte length/data format, and make it ASCIIZ.

char *MakeASCIIZ(unsigned char *LString)
{
    char *ASCIIZString;
    unsigned char StringLength;

    StringLength = *LString++;

    if ((ASCIIZString = malloc((int) StringLength + 1)) == NULL) 
        return (NULL);

    strncpy(ASCIIZString, (signed char *) LString, StringLength);

    ASCIIZString[StringLength] = '\0';

    return (ASCIIZString);
}

// Output -- Write to the output stream. This function adds an exception-
// handling layer to disk IO. It handles abnormal program termination, and 
// warnings to both stderr and output. Three types of message can be handled: 
// Message, simply printed to a file; Warning, print to file AND stderr; 
// Error, same as warning, but terminate with abnormal exit code.

void Output(MESSAGETYPE MsgType, FILE *Stream, char *OutputFormat, ...)
{
    char OutputBuffer[133];
    va_list VarArgP;

    va_start(VarArgP, OutputFormat);
    vsprintf(OutputBuffer, OutputFormat, VarArgP);

    // If this is (non-fatal) warning or (fatal) error, also send it to stderr 
    if (MsgType != Message)
       fprintf(stderr, "\a%s", OutputBuffer);

    // In any case: attempt to print message to output file.  Exception check. 
    if (Stream != NOFILE)
       if ((size_t) fprintf(Stream, OutputBuffer) != strlen(OutputBuffer))
          {
          fprintf(stderr, "\aDisk Write Failure!\n");
          abort();
          }

    /* If this was (fatal) error message, abort on the spot */
    if (MsgType == Error)
       {
       flushall();
       fcloseall();
       abort();
       }

    va_end(VarArgP);
}

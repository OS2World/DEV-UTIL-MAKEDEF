/****************************************************************************

   NAME    : MAKEDEF
   FROM    : Reiner Kissel + Peter Reinhardt BASF AG Ludwigshafen

****************************************************************************/
char copyright[] = "MAKEDEF.EXE  Version November 1992";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include "objutils.h"

/* ------ Defines --------*/
#define EXTDEF     0x001
#define PUBDEF     0x002
#define COMDEF     0x004
#define E_PDEF     0x008
#define WRITTEN    0x010

#define MAXSTRING  256

/* ------ Global variables --------*/
struct nam
{
   char * name;
   int  flags;
   struct nam *next;
};
typedef struct nam namen;

namen *start_namen = NULL;
namen *start_com = NULL;
namen *n, *zn;

OMFHEADER ObjRecHdr;

/* ----- Prototypes ------------*/
int main(int, char **);
void print_help(void);
int get_name(FILE *fp_obj, int *reclen, namen **start_list, int flag);
int get_index(FILE *fp_obj, int *reclen);
int get_number(FILE *fp_obj, int *reclen);
int get_length(FILE *fp_obj, int *reclen);
void process_obj(char *objname, char *library, FILE *fp_def, int req_flag);
void process_lib(char *libname, char *dllname, FILE *fp_def);
void DumpSymbolDictionary(LIBHDR *LibHeader, FILE *InLibFH, FILE *fp_def);
void PrintDefHeader(char *FileName, char *LibName, FILE *fp_def);
void InsertInList(namen **start_list, char *pbuf, int flag);

/* ----- main ------------------*/
int main(int argc, char **argv)
{
  FILE *fp_objlist;
  char tmp[MAXSTRING];
  FILE *fp_def = stdout;
  int args;
  int req_flag=0;
  char *library = "";

  if (argc<2)
    print_help();

  for(args = 1; args < argc; args++)
  {
    strupr(argv[args]);

    if (strlen(argv[args]) < 2 || argv[args][0] == '/')
    {
       if (stricmp(argv[args],"/L")==0)
       {
         ++args;
         if (args<argc)
           library=argv[args];
       }
       else if (stricmp(argv[args],"/D")==0)
       {
         ++args;
         if (args<argc)
         {
           fp_def=fopen(argv[args],"w");
           if (!fp_def)
           {
             fprintf(stderr,"Open error: Definition file %s\n",argv[args]);
             exit(1);
           }
         }
       }
       else if (stricmp(argv[args],"/P")==0)
         req_flag|=PUBDEF;
       else if (stricmp(argv[args],"/C")==0)
         req_flag|=COMDEF;
       else if (stricmp(argv[args],"/E")==0)
         req_flag|=EXTDEF;
       else if (stricmp(argv[args],"/O")==0)
       {
          ++args;
          if (args<argc)
          {
             fp_objlist=fopen(argv[args],"r");
             if (!fp_objlist)
             {
                fprintf(stderr,"Open error: Objectlist file %s\n",argv[args]);
                exit(1);
             }
             else
             {
                while( 1 )
                {
                   if( fgets( tmp, MAXSTRING - 1, fp_objlist ) == NULL )
                     if( feof( fp_objlist ) )
                         break;
                   tmp[strlen(tmp) - 1] = '\0';
                   process_obj(tmp, library, fp_def, req_flag);
                }
             }
             fclose(fp_objlist);
          }
       }
       else if (stricmp(argv[args],"/B")==0)
       {
          ++args;
          if (args<argc)
          {
             fp_objlist=fopen(argv[args],"r");
             if (!fp_objlist)
             {
                fprintf(stderr,"Open error: Librarylist file %s\n",argv[args]);
                exit(1);
             }
             else
             {
                while( 1 )
                {
                   if( fgets( tmp, MAXSTRING - 1, fp_objlist ) == NULL )
                     if( feof( fp_objlist ) )
                         break;
                   tmp[strlen(tmp) - 1] = '\0';
                   process_lib(tmp, library, fp_def);
                }
             }
             fclose(fp_objlist);
          }
       }
       else
         print_help();
       continue;
    }

    if (strstr(argv[args], ".OBJ"))
       process_obj(argv[args], library, fp_def, req_flag);
    else if (strstr(argv[args], ".LIB"))
       process_lib(argv[args], library, fp_def);
    else
       print_help();

  } /* end args */

  for (n = start_com; n; )
     {
     zn = n;
     n  = n->next;
     free(zn->name);
     free(zn);
     }

  start_com = NULL;

  return 0;
}

/*---------------------------------------------------------------------------*/
void print_help(void)
{
  int i;
  static char *text[]={copyright,
"Analyse .OBJ or .LIB Files to produce a .DEF File with its Exports",
"Syntax: MAKEDEF [/l libname] [/d deffile] [/o ol] [/b ll] <opt> objfile(s)",
"  libname       default: basename of the first objecfile",
"  deffile       default: stdout",
"  ol : file with list of objects to process",
"  ll : file with list of libraries to process (<opt> ignored)",
"  opt:",
"  /p : Export public names   (Global Vars initialized)",
"  /c : Export communal names (Global Vars uninitialized or C++ static Vars)",
"  /e : Emit external names as comments (External Funcs and Vars)",
"  /? : This information",
"The exported names in the produced .DEF file are in the order of the analysed",
"objektfiles and their types. Comment headers show the object names and the",
"Symbol types.",
"Global function names (these are public and external) will allways be exported.",
"Global initialized variables (option /p) should be exported to reference ",
"___vtbl_ entries from Glockenspiel C++.",
"Global uninitialized variables (option /c) will be exported once per name",
"and regardless of their appearance in other .OBJ files (shown as a comment).",
"External functions and variables (option /e) will only be emitted as comments",
"for information.",
NULL};

  for (i=0;text[i];i++)
    fprintf(stderr,"%s\n",text[i]);

  exit(1);
}

/*---------------------------------------------------------------------------*/
int get_name(FILE *fp_obj, int *reclen, namen **start_list, int flag)
{
  int len;
  int c = 0;
  char *pbuf, *p;
  namen **n,*zn;

  len = getc(fp_obj);
  (*reclen)--;

  if (len == EOF)
     return len;

  pbuf = malloc(len + 1);

  if (!pbuf)
     {
     fprintf(stderr,"malloc failure\n");
     exit(1);
     }

  (*reclen) -= len;

  if ((*reclen) <= 0)
     {
     fprintf(stderr,"*** %s ***: Unexpected end of records\n",
             flag==PUBDEF ? "PUBDEF" : (flag==COMDEF ? "COMDEF" : "EXTDEF"));
     exit(1);
     }

  if (fread((void *)pbuf, len, 1, fp_obj) != 1)
     {
     free(pbuf);
     return EOF;
     }

  pbuf[len] = 0;

  InsertInList(start_list, pbuf, flag);

  return 0;
}

/*---------------------------------------------------------------------------*/
int get_index(FILE *fp_obj, int *reclen)
{
  int i,j;

  i = getc(fp_obj);
  (*reclen)--;

  if (i & 0x80)
     {
     j = getc(fp_obj);
     (*reclen)--;
     }

  return i;
}

/*---------------------------------------------------------------------------*/
int get_number(FILE *fp_obj, int *reclen)
{
  fseek(fp_obj, 2, SEEK_CUR);
  (*reclen)--;
  (*reclen)--;

  return 0;
}

/*---------------------------------------------------------------------------*/
int get_length(FILE *fp_obj, int *reclen)
{
  int i;

  i = getc(fp_obj);
  (*reclen)--;

  switch (i)
     {
     case 0x81:
          fseek(fp_obj, 2, SEEK_CUR);
          (*reclen)--;
          (*reclen)--;
          break;

     case 0x84:
          fseek(fp_obj, 3, SEEK_CUR);
          (*reclen)--;
          (*reclen)--;
          (*reclen)--;
          break;

     case 0x88:
          fseek(fp_obj, 4, SEEK_CUR);
          (*reclen)--;
          (*reclen)--;
          (*reclen)--;
          (*reclen)--;
          break;
     }

  return (i);
}

/*---------------------------------------------------------------------------*/
void process_obj(char *objname, char *library, FILE *fp_def, int req_flag)
{
    FILE *fp_obj;
    int i,j;
    int test_flag;
    int rectyp,reclen;
    int c,num,newdef;
    int Ende = 0;
    static int header_request = 1;

    fp_obj = fopen(objname,"rb");

    if (!fp_obj)
       {
       fprintf(stderr,"Object file %s not found\n",objname);
       exit(1);
       }

    if (header_request)
       {
       header_request=0;

       PrintDefHeader(objname, library, fp_def);
       }

    fprintf(stderr,"Processing %s\n",objname);

    test_flag=0;

    while (!Ende)
       {
       if (fread((void *)&ObjRecHdr, sizeof(OMFHEADER), 1, fp_obj) != 1)
          {
          fprintf(stderr,"Unexpected EOF in objectheader.\n");
          exit(1);
          }

       // default = old obj-format
       newdef = 0;
       rectyp = ObjRecHdr.RecType;
       reclen = ObjRecHdr.RecLength;

       switch (rectyp)
          {
          case 0x08C:                  /* EXTDEF OS/2 1.3 and OS/2 2.0 */
               while (reclen > 1)
                  {
                  c = get_name(fp_obj,&reclen,&start_namen,EXTDEF);
                  c = get_index(fp_obj,&reclen); /* Type index */
                  }
               break;

          case 0x091:                  /* PUBDEF  format from OS/2 2.0*/
               newdef = 1;
          case 0x090:                  /* PUBDEF  old format*/
               c = get_index(fp_obj,&reclen);   /*Grp Idx*/
               c = get_index(fp_obj,&reclen);   /*Seg Idx*/

               if (c == 0)                      /* cond. Frame */
                  c = get_number(fp_obj,&reclen);

               while (reclen > 1)
                  {
                  c = get_name(fp_obj,&reclen,&start_namen,PUBDEF);
                  c = get_number(fp_obj,&reclen);      /* Offset */

                  if (newdef)                          /* Offset 2 (32 Bit) */
                     c = get_number(fp_obj,&reclen);

                  c = get_index(fp_obj,&reclen);       /* Type index */
                  } /* while (reclen--) */

               break;

          case 0x0B0:                  /* COMDEF OS/2 1.3 and OS/2 2.0 */
               if (!(req_flag&COMDEF))
                  {
                  fseek(fp_obj, (long)reclen - 1, SEEK_CUR);
                  break;
                  }

               while (reclen > 1)
                  {
                  test_flag |= COMDEF;
                  c = get_name(fp_obj,&reclen,&start_com,COMDEF);
                  c = get_index(fp_obj,&reclen);    /* Type index */
                  c = getc(fp_obj);                 /* Data Seg Type */
                  reclen--;

                  switch (c)
                     {
                     case 0x062:        /* (NEAR) */
                          c = get_length(fp_obj,&reclen);
                          break;

                     case 0x061:        /* (FAR)  */
                          c = get_length(fp_obj,&reclen);
                          c = get_length(fp_obj,&reclen);
                          break;

                     default:
                          fprintf(stderr,"Unexpected Data Seg Type %.2XH in RECTYP %.2XH\n",
                                  c,rectyp);
                          exit(1);
                          break;
                     }

                  } /* while (reclen > 1) */
               break;

          case 0x8a:
          case 0x8b:
               Ende = 1;

          default:
               fseek(fp_obj, (long)reclen - 1, SEEK_CUR);
               break;

          } /* switch (rectyp) */

       if (getc(fp_obj) == EOF) /*checksum*/
          {
          fprintf(stderr,"Unexpected EOF in RECTYP %.2XH\n",rectyp);
          exit(1);
          }

       } /* while (!Ende) */

    fclose(fp_obj);

    for (n = start_namen; n; )
       {
       if (n->flags & EXTDEF && n->flags & PUBDEF)
          test_flag |= E_PDEF;
       else if (n->flags & req_flag & PUBDEF)
          test_flag |= PUBDEF;
       else if (n->flags & req_flag & EXTDEF)
          test_flag |= EXTDEF;
       n = n->next;
       }

    if (test_flag)
       fprintf(fp_def,"; OBJ-file: %s\n",objname);

    if (test_flag & E_PDEF)
       {
       fprintf(fp_def,";   Names External and Public (Global Functions):\n");
       for (n = start_namen; n; )
          {
          if (n->flags & EXTDEF && n->flags & PUBDEF)
             fprintf(fp_def,"\t%s\n",n->name);
          n = n->next;
          }
       }

    if (test_flag & PUBDEF)
       {
       fprintf(fp_def,";   Names Public (Global Variables initialized):\n");
       for (n = start_namen; n; )
          {
          if (n->flags & PUBDEF && !(n->flags & EXTDEF))
          fprintf(fp_def,"\t%s\n",n->name);
          n = n->next;
          }
       }

    if (test_flag & COMDEF)
       {
       fprintf(fp_def,";   Names Communal (Global Variables uninitialized):\n");
       for (n = start_com; n; )
          {
          if (n->flags & COMDEF)
             {
             if (n->flags & WRITTEN)
                fprintf(fp_def,";\t%s\n",n->name);
             else
                fprintf(fp_def,"\t%s\n",n->name);
             n->flags = WRITTEN;
             }
          n = n->next;
          }
       }

    if (test_flag & EXTDEF)
       fprintf(fp_def,";   Names External (External Functions and Variables):\n");

    for (n = start_namen; n; )
       {
       if (n->flags & req_flag & EXTDEF && !(n->flags & PUBDEF))
          fprintf(fp_def,";\t%s\n",n->name);
       zn = n;
       n  = n->next;
       free(zn->name);
       free(zn);
       }

    start_namen=NULL;
}

/*---------------------------------------------------------------------------*/
void process_lib(char *libname, char *dllname, FILE *fp_def)
{
    FILE *InLibFH;
    LIBHDR LibHeader;
    int i,j;
    static int header_request = 1;

    if (header_request)
       {
       header_request = 0;
       PrintDefHeader(libname, dllname, fp_def);
       }

    if ((InLibFH = fopen(libname, "rb")) == NULL)
        Output(Error, NOFILE, "Couldn't Open %s.\n", libname);

    fprintf(stderr,"Processing %s\n",libname);

    fprintf(fp_def,"; LIB-file: %s\n",libname);
    fprintf(fp_def,";   Names External and Public (Global Functions):\n");

    GetLibHeader(&LibHeader, InLibFH);
    DumpSymbolDictionary(&LibHeader, InLibFH, fp_def);

    for (n = start_namen; n; )
       {
       fprintf(fp_def,"\t%s\n",n->name);
       zn = n;
       n  = n->next;
       free(zn->name);
       free(zn);
       }

    start_namen = NULL;
}

/*---------------------------------------------------------------------------*/
//  DumpSymbolDictionary  --  Print out an entire Symbol Dictionary
void DumpSymbolDictionary(LIBHDR *LibHeader, FILE *InLibFH, FILE *fp_def)
{
    int BlockIdx, BucketIdx;
    DICTENTRY DictEntry;
    char *SymbolP;

    for (BlockIdx = 0; BlockIdx < LibHeader->NumDictBlocks; BlockIdx++) 
       for (BucketIdx = 0; BucketIdx < NUMBUCKETS; BucketIdx++)
          {
          DictEntry = GetSymDictEntry(BlockIdx, BucketIdx, LibHeader, InLibFH);

          if (DictEntry.IsFound == false)
             continue;

          // Get the symbol name
          SymbolP = MakeASCIIZ(DictEntry.SymbolP);

          // discard modul entry (ends with !)
          if (SymbolP[strlen(SymbolP) - 1] != '!')
             InsertInList(&start_namen, SymbolP, EXTDEF | PUBDEF);
          }
}

/*---------------------------------------------------------------------------*/
void PrintDefHeader(char *FileName, char *LibName, FILE *fp_def)
{
    int  i,j;
    char *Name = LibName;

    if (*LibName == '\0')
       {
       Name = malloc(9);

       for (i = strlen(FileName); i && FileName[i] != '\\'; i--);
          if (FileName[i] == '\\')
             i++;

       for (j = 0; j < 8 && FileName[i] != '.' && FileName[i]; j++, i++)
           Name[j] = FileName[i];

       Name[j] = '\0';
       }

    fprintf(fp_def,"LIBRARY\t%s\tINITINSTANCE\n",Name);
    fprintf(fp_def,"DESCRIPTION\t\'%s.DLL --- Copyright <Your Copyright>\'\n",Name);
    fprintf(fp_def,"CODE\tSHARED\n");
    fprintf(fp_def,"DATA\tNONSHARED\n");
    fprintf(fp_def,"EXPORTS\n");

    if (*LibName == '\0')
       free(Name);
}

/*---------------------------------------------------------------------------*/
void InsertInList(namen **start_list, char *pbuf, int flag)
{
  namen **n,*zn;

  for (n = start_list; (*n); n = &((*n)->next))
     {
     if (strcmp(pbuf,(*n)->name) > 0)
        continue;

     if (strcmp(pbuf,(*n)->name) == 0)
        {
        (*n)->flags |= flag;
        return;
        }

     zn   = *n;
     (*n) = NULL;
     (*n) = malloc(sizeof(namen));

     if (!(*n))
        {
        fprintf(stderr,"malloc failure\n");
        exit(1);
        }

     (*n)->name  = pbuf;
     (*n)->flags = flag;
     (*n)->next  = zn;
     return;
     }

  (*n) = malloc(sizeof(namen));

  if (!(*n))
     {
     fprintf(stderr,"malloc failure\n");
     exit(1);
     }

  (*n)->name  = pbuf;
  (*n)->flags = flag;
  (*n)->next  = NULL;
}

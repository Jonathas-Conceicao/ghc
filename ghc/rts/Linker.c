/* -----------------------------------------------------------------------------
 * $Id: Linker.c,v 1.82 2002/02/04 16:30:20 sewardj Exp $
 *
 * (c) The GHC Team, 2000, 2001
 *
 * RTS Object Linker
 *
 * ---------------------------------------------------------------------------*/

#include "PosixSource.h"
#include "Rts.h"
#include "RtsFlags.h"
#include "HsFFI.h"
#include "Hash.h"
#include "Linker.h"
#include "LinkerInternals.h"
#include "RtsUtils.h"
#include "StoragePriv.h"
#include "Schedule.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#if defined(linux_TARGET_OS) || defined(solaris2_TARGET_OS) || defined(freebsd_TARGET_OS)
#  define OBJFORMAT_ELF
#elif defined(cygwin32_TARGET_OS) || defined (mingw32_TARGET_OS)
#  define OBJFORMAT_PEi386
#  include <windows.h>
#endif

/* Hash table mapping symbol names to Symbol */
/*Str*/HashTable *symhash;

#if defined(OBJFORMAT_ELF)
static int ocVerifyImage_ELF    ( ObjectCode* oc );
static int ocGetNames_ELF       ( ObjectCode* oc );
static int ocResolve_ELF        ( ObjectCode* oc );
#elif defined(OBJFORMAT_PEi386)
static int ocVerifyImage_PEi386 ( ObjectCode* oc );
static int ocGetNames_PEi386    ( ObjectCode* oc );
static int ocResolve_PEi386     ( ObjectCode* oc );
#endif

/* -----------------------------------------------------------------------------
 * Built-in symbols from the RTS
 */

typedef struct _RtsSymbolVal {
    char   *lbl;
    void   *addr;
} RtsSymbolVal;


#if !defined(PAR)
#define Maybe_ForeignObj        SymX(mkForeignObjzh_fast)

#define Maybe_Stable_Names      SymX(mkWeakzh_fast)			\
      				SymX(makeStableNamezh_fast)		\
      				SymX(finalizzeWeakzh_fast)
#else
/* These are not available in GUM!!! -- HWL */
#define Maybe_ForeignObj
#define Maybe_Stable_Names
#endif

#if !defined (mingw32_TARGET_OS)

#define RTS_POSIX_ONLY_SYMBOLS                  \
      SymX(stg_sig_install)			\
      Sym(nocldstop)
#define RTS_MINGW_ONLY_SYMBOLS /**/

#else

#define RTS_POSIX_ONLY_SYMBOLS

/* These are statically linked from the mingw libraries into the ghc
   executable, so we have to employ this hack. */
#define RTS_MINGW_ONLY_SYMBOLS                  \
      SymX(memset)                              \
      SymX(inet_ntoa)                           \
      SymX(inet_addr)                           \
      SymX(htonl)                               \
      SymX(recvfrom)                            \
      SymX(listen)                              \
      SymX(bind)                                \
      SymX(shutdown)                            \
      SymX(connect)                             \
      SymX(htons)                               \
      SymX(ntohs)                               \
      SymX(getservbyname)                       \
      SymX(getservbyport)                       \
      SymX(getprotobynumber)                    \
      SymX(getprotobyname)                      \
      SymX(gethostbyname)                       \
      SymX(gethostbyaddr)                       \
      SymX(gethostname)                         \
      SymX(strcpy)                              \
      SymX(strncpy)                             \
      SymX(abort)                               \
      Sym(_alloca)                              \
      Sym(isxdigit)                             \
      Sym(isupper)                              \
      Sym(ispunct)                              \
      Sym(islower)                              \
      Sym(isspace)                              \
      Sym(isprint)                              \
      Sym(isdigit)                              \
      Sym(iscntrl)                              \
      Sym(isalpha)                              \
      Sym(isalnum)                              \
      SymX(strcmp)                              \
      SymX(memmove)                             \
      SymX(realloc)                             \
      SymX(malloc)                              \
      SymX(pow)                                 \
      SymX(tanh)                                \
      SymX(cosh)                                \
      SymX(sinh)                                \
      SymX(atan)                                \
      SymX(acos)                                \
      SymX(asin)                                \
      SymX(tan)                                 \
      SymX(cos)                                 \
      SymX(sin)                                 \
      SymX(exp)                                 \
      SymX(log)                                 \
      SymX(sqrt)                                \
      SymX(memcpy)                              \
      Sym(mktime)                               \
      Sym(_imp___timezone)                      \
      Sym(_imp___tzname)                        \
      Sym(_imp___iob)                           \
      Sym(localtime)                            \
      Sym(gmtime)                               \
      Sym(opendir)                              \
      Sym(readdir)                              \
      Sym(closedir)                             \
      Sym(__divdi3)                             \
      Sym(__udivdi3)                            \
      Sym(__moddi3)                             \
      Sym(__umoddi3)
#endif

#ifndef SMP
# define MAIN_CAP_SYM SymX(MainCapability)
#else
# define MAIN_CAP_SYM
#endif

#define RTS_SYMBOLS				\
      Maybe_ForeignObj				\
      Maybe_Stable_Names			\
      Sym(StgReturn)				\
      Sym(__stginit_PrelGHC)			\
      Sym(init_stack)				\
      SymX(__stg_chk_0)				\
      SymX(__stg_chk_1)				\
      Sym(stg_enterStackTop)			\
      SymX(stg_gc_d1)				\
      SymX(stg_gc_l1)				\
      SymX(__stg_gc_enter_1)			\
      SymX(stg_gc_f1)				\
      SymX(stg_gc_noregs)			\
      SymX(stg_gc_seq_1)			\
      SymX(stg_gc_unbx_r1)			\
      SymX(stg_gc_unpt_r1)			\
      SymX(stg_gc_ut_0_1)			\
      SymX(stg_gc_ut_1_0)			\
      SymX(stg_gen_chk)				\
      SymX(stg_yield_to_interpreter)		\
      SymX(ErrorHdrHook)			\
      MAIN_CAP_SYM                              \
      SymX(MallocFailHook)			\
      SymX(NoRunnableThreadsHook)		\
      SymX(OnExitHook)				\
      SymX(OutOfHeapHook)			\
      SymX(PatErrorHdrHook)			\
      SymX(PostTraceHook)			\
      SymX(PreTraceHook)			\
      SymX(StackOverflowHook)			\
      SymX(__encodeDouble)			\
      SymX(__encodeFloat)			\
      SymX(__gmpn_gcd_1)			\
      SymX(__gmpz_cmp)				\
      SymX(__gmpz_cmp_si)			\
      SymX(__gmpz_cmp_ui)			\
      SymX(__gmpz_get_si)			\
      SymX(__gmpz_get_ui)			\
      SymX(__int_encodeDouble)			\
      SymX(__int_encodeFloat)			\
      SymX(andIntegerzh_fast)			\
      SymX(blockAsyncExceptionszh_fast)		\
      SymX(catchzh_fast)			\
      SymX(cmp_thread)				\
      SymX(complementIntegerzh_fast)		\
      SymX(cmpIntegerzh_fast)	        	\
      SymX(cmpIntegerIntzh_fast)	      	\
      SymX(createAdjustor)			\
      SymX(decodeDoublezh_fast)			\
      SymX(decodeFloatzh_fast)			\
      SymX(defaultsHook)			\
      SymX(delayzh_fast)			\
      SymX(deRefWeakzh_fast)			\
      SymX(deRefStablePtrzh_fast)		\
      SymX(divExactIntegerzh_fast)		\
      SymX(divModIntegerzh_fast)		\
      SymX(forkzh_fast)				\
      SymX(freeHaskellFunctionPtr)		\
      SymX(freeStablePtr)		        \
      SymX(gcdIntegerzh_fast)			\
      SymX(gcdIntegerIntzh_fast)		\
      SymX(gcdIntzh_fast)			\
      SymX(getProgArgv)				\
      SymX(getStablePtr)			\
      SymX(int2Integerzh_fast)			\
      SymX(integer2Intzh_fast)			\
      SymX(integer2Wordzh_fast)			\
      SymX(isDoubleDenormalized)		\
      SymX(isDoubleInfinite)			\
      SymX(isDoubleNaN)				\
      SymX(isDoubleNegativeZero)		\
      SymX(isEmptyMVarzh_fast)			\
      SymX(isFloatDenormalized)			\
      SymX(isFloatInfinite)			\
      SymX(isFloatNaN)				\
      SymX(isFloatNegativeZero)			\
      SymX(killThreadzh_fast)			\
      SymX(makeStablePtrzh_fast)		\
      SymX(minusIntegerzh_fast)			\
      SymX(mkApUpd0zh_fast)			\
      SymX(myThreadIdzh_fast)			\
      SymX(newArrayzh_fast)			\
      SymX(newBCOzh_fast)			\
      SymX(newByteArrayzh_fast)			\
      SymX(newCAF)				\
      SymX(newMVarzh_fast)			\
      SymX(newMutVarzh_fast)			\
      SymX(newPinnedByteArrayzh_fast)		\
      SymX(orIntegerzh_fast)			\
      SymX(performGC)				\
      SymX(plusIntegerzh_fast)			\
      SymX(prog_argc)				\
      SymX(prog_argv)				\
      SymX(putMVarzh_fast)			\
      SymX(quotIntegerzh_fast)			\
      SymX(quotRemIntegerzh_fast)		\
      SymX(raisezh_fast)			\
      SymX(remIntegerzh_fast)			\
      SymX(resetNonBlockingFd)			\
      SymX(resumeThread)			\
      SymX(rts_apply)				\
      SymX(rts_checkSchedStatus)		\
      SymX(rts_eval)				\
      SymX(rts_evalIO)				\
      SymX(rts_evalLazyIO)			\
      SymX(rts_eval_)				\
      SymX(rts_getAddr)				\
      SymX(rts_getBool)				\
      SymX(rts_getChar)				\
      SymX(rts_getDouble)			\
      SymX(rts_getFloat)			\
      SymX(rts_getInt)				\
      SymX(rts_getInt32)			\
      SymX(rts_getPtr)				\
      SymX(rts_getStablePtr)			\
      SymX(rts_getThreadId)			\
      SymX(rts_getWord)				\
      SymX(rts_getWord32)			\
      SymX(rts_mkAddr)				\
      SymX(rts_mkBool)				\
      SymX(rts_mkChar)				\
      SymX(rts_mkDouble)			\
      SymX(rts_mkFloat)				\
      SymX(rts_mkInt)				\
      SymX(rts_mkInt16)				\
      SymX(rts_mkInt32)				\
      SymX(rts_mkInt64)				\
      SymX(rts_mkInt8)				\
      SymX(rts_mkPtr)				\
      SymX(rts_mkStablePtr)			\
      SymX(rts_mkString)			\
      SymX(rts_mkWord)				\
      SymX(rts_mkWord16)			\
      SymX(rts_mkWord32)			\
      SymX(rts_mkWord64)			\
      SymX(rts_mkWord8)				\
      SymX(run_queue_hd)			\
      SymX(setProgArgv)				\
      SymX(shutdownHaskellAndExit)		\
      SymX(stable_ptr_table)			\
      SymX(stackOverflow)			\
      SymX(stg_CAF_BLACKHOLE_info)		\
      SymX(stg_CHARLIKE_closure)		\
      SymX(stg_EMPTY_MVAR_info)			\
      SymX(stg_IND_STATIC_info)			\
      SymX(stg_INTLIKE_closure)			\
      SymX(stg_MUT_ARR_PTRS_FROZEN_info)	\
      SymX(stg_WEAK_info)                       \
      SymX(stg_ap_1_upd_info)			\
      SymX(stg_ap_2_upd_info)			\
      SymX(stg_ap_3_upd_info)			\
      SymX(stg_ap_4_upd_info)			\
      SymX(stg_ap_5_upd_info)			\
      SymX(stg_ap_6_upd_info)			\
      SymX(stg_ap_7_upd_info)			\
      SymX(stg_ap_8_upd_info)			\
      SymX(stg_exit)				\
      SymX(stg_sel_0_upd_info)			\
      SymX(stg_sel_10_upd_info)			\
      SymX(stg_sel_11_upd_info)			\
      SymX(stg_sel_12_upd_info)			\
      SymX(stg_sel_13_upd_info)			\
      SymX(stg_sel_14_upd_info)			\
      SymX(stg_sel_15_upd_info)			\
      SymX(stg_sel_1_upd_info)			\
      SymX(stg_sel_2_upd_info)			\
      SymX(stg_sel_3_upd_info)			\
      SymX(stg_sel_4_upd_info)			\
      SymX(stg_sel_5_upd_info)			\
      SymX(stg_sel_6_upd_info)			\
      SymX(stg_sel_7_upd_info)			\
      SymX(stg_sel_8_upd_info)			\
      SymX(stg_sel_9_upd_info)			\
      SymX(stg_seq_frame_info)			\
      SymX(stg_upd_frame_info)			\
      SymX(__stg_update_PAP)			\
      SymX(suspendThread)			\
      SymX(takeMVarzh_fast)			\
      SymX(timesIntegerzh_fast)			\
      SymX(tryPutMVarzh_fast)			\
      SymX(tryTakeMVarzh_fast)			\
      SymX(unblockAsyncExceptionszh_fast)	\
      SymX(unsafeThawArrayzh_fast)		\
      SymX(waitReadzh_fast)			\
      SymX(waitWritezh_fast)			\
      SymX(word2Integerzh_fast)			\
      SymX(xorIntegerzh_fast)			\
      SymX(yieldzh_fast)

#ifndef SUPPORT_LONG_LONGS
#define RTS_LONG_LONG_SYMS /* nothing */
#else
#define RTS_LONG_LONG_SYMS			\
      SymX(int64ToIntegerzh_fast)		\
      SymX(word64ToIntegerzh_fast)
#endif /* SUPPORT_LONG_LONGS */

/* entirely bogus claims about types of these symbols */
#define Sym(vvv)  extern void (vvv);
#define SymX(vvv) /**/
RTS_SYMBOLS
RTS_LONG_LONG_SYMS
RTS_POSIX_ONLY_SYMBOLS
RTS_MINGW_ONLY_SYMBOLS
#undef Sym
#undef SymX

#ifdef LEADING_UNDERSCORE
#define MAYBE_LEADING_UNDERSCORE_STR(s) ("_" s)
#else
#define MAYBE_LEADING_UNDERSCORE_STR(s) (s)
#endif

#define Sym(vvv) { MAYBE_LEADING_UNDERSCORE_STR(#vvv), \
                    (void*)(&(vvv)) },
#define SymX(vvv) Sym(vvv)

static RtsSymbolVal rtsSyms[] = {
      RTS_SYMBOLS
      RTS_LONG_LONG_SYMS
      RTS_POSIX_ONLY_SYMBOLS
      RTS_MINGW_ONLY_SYMBOLS
      { 0, 0 } /* sentinel */
};

/* -----------------------------------------------------------------------------
 * Insert symbols into hash tables, checking for duplicates.
 */
static void ghciInsertStrHashTable ( char* obj_name,
                                     HashTable *table,
                                     char* key, 
                                     void *data
				   )
{
   if (lookupHashTable(table, (StgWord)key) == NULL)
   {
      insertStrHashTable(table, (StgWord)key, data);
      return;
   }
   fprintf(stderr, 
      "\n\n"
      "GHCi runtime linker: fatal error: I found a duplicate definition for symbol\n"
      "   %s\n"
      "whilst processing object file\n"
      "   %s\n"
      "This could be caused by:\n"
      "   * Loading two different object files which export the same symbol\n"
      "   * Specifying the same object file twice on the GHCi command line\n"
      "   * An incorrect `package.conf' entry, causing some object to be\n"
      "     loaded twice.\n"
      "GHCi cannot safely continue in this situation.  Exiting now.  Sorry.\n"
      "\n",
      (char*)key,
      obj_name
   );
   exit(1);
}


/* -----------------------------------------------------------------------------
 * initialize the object linker
 */
#if defined(OBJFORMAT_ELF)
static void *dl_prog_handle;
#endif

void
initLinker( void )
{
    RtsSymbolVal *sym;

    symhash = allocStrHashTable();

    /* populate the symbol table with stuff from the RTS */
    for (sym = rtsSyms; sym->lbl != NULL; sym++) {
	ghciInsertStrHashTable("(GHCi built-in symbols)",
                               symhash, sym->lbl, sym->addr);
    }
#   if defined(OBJFORMAT_ELF)
    dl_prog_handle = dlopen(NULL, RTLD_LAZY);
#   endif
}

/* -----------------------------------------------------------------------------
 * Add a DLL from which symbols may be found.  In the ELF case, just
 * do RTLD_GLOBAL-style add, so no further messing around needs to
 * happen in order that symbols in the loaded .so are findable --
 * lookupSymbol() will subsequently see them by dlsym on the program's
 * dl-handle.  Returns NULL if success, otherwise ptr to an err msg.
 *
 * In the PEi386 case, open the DLLs and put handles to them in a 
 * linked list.  When looking for a symbol, try all handles in the
 * list.
 */

#if defined(OBJFORMAT_PEi386)
/* A record for storing handles into DLLs. */

typedef
   struct _OpenedDLL {
      char*              name;
      struct _OpenedDLL* next;
      HINSTANCE instance;
   } 
   OpenedDLL;

/* A list thereof. */
static OpenedDLL* opened_dlls = NULL;
#endif



char*
addDLL ( __attribute((unused)) char* path, char* dll_name )
{
#  if defined(OBJFORMAT_ELF)
   void *hdl;
   char *buf;
   char *errmsg;

   if (path == NULL || strlen(path) == 0) {
      buf = stgMallocBytes(strlen(dll_name) + 10, "addDll");
      sprintf(buf, "lib%s.so", dll_name);
   } else {
      buf = stgMallocBytes(strlen(path) + 1 + strlen(dll_name) + 10, "addDll");
      sprintf(buf, "%s/lib%s.so", path, dll_name);
   }
   hdl = dlopen(buf, RTLD_NOW | RTLD_GLOBAL );
   free(buf);
   if (hdl == NULL) {
      /* dlopen failed; return a ptr to the error msg. */
      errmsg = dlerror();
      if (errmsg == NULL) errmsg = "addDLL: unknown error";
      return errmsg;
   } else {
      return NULL;
   }
   /*NOTREACHED*/

#  elif defined(OBJFORMAT_PEi386)

   /* Add this DLL to the list of DLLs in which to search for symbols.
      The path argument is ignored. */
   char*      buf;
   OpenedDLL* o_dll;
   HINSTANCE  instance;

   /* fprintf(stderr, "\naddDLL; path=`%s', dll_name = `%s'\n", path, dll_name); */

   /* See if we've already got it, and ignore if so. */
   for (o_dll = opened_dlls; o_dll != NULL; o_dll = o_dll->next) {
      if (0 == strcmp(o_dll->name, dll_name))
         return NULL;
   }

   buf = stgMallocBytes(strlen(dll_name) + 10, "addDLL");
   sprintf(buf, "%s.DLL", dll_name);
   instance = LoadLibrary(buf);
   free(buf);
   if (instance == NULL) {
     /* LoadLibrary failed; return a ptr to the error msg. */
     return "addDLL: unknown error";
   }

   o_dll = stgMallocBytes( sizeof(OpenedDLL), "addDLL" );
   o_dll->name     = stgMallocBytes(1+strlen(dll_name), "addDLL");
   strcpy(o_dll->name, dll_name);
   o_dll->instance = instance;
   o_dll->next     = opened_dlls;
   opened_dlls     = o_dll;

   return NULL;
#  else
   barf("addDLL: not implemented on this platform");
#  endif
}

/* -----------------------------------------------------------------------------
 * lookup a symbol in the hash table
 */  
void *
lookupSymbol( char *lbl )
{
    void *val;
    ASSERT(symhash != NULL);
    val = lookupStrHashTable(symhash, lbl);

    if (val == NULL) {
#       if defined(OBJFORMAT_ELF)
	return dlsym(dl_prog_handle, lbl);
#       elif defined(OBJFORMAT_PEi386)
        OpenedDLL* o_dll;
        void* sym;
        for (o_dll = opened_dlls; o_dll != NULL; o_dll = o_dll->next) {
	  /* fprintf(stderr, "look in %s for %s\n", o_dll->name, lbl); */
           if (lbl[0] == '_') {
              /* HACK: if the name has an initial underscore, try stripping
                 it off & look that up first. I've yet to verify whether there's
                 a Rule that governs whether an initial '_' *should always* be
                 stripped off when mapping from import lib name to the DLL name.
              */
              sym = GetProcAddress(o_dll->instance, (lbl+1));
              if (sym != NULL) {
		/*fprintf(stderr, "found %s in %s\n", lbl+1,o_dll->name); fflush(stderr);*/
		return sym;
	      } 
           }
           sym = GetProcAddress(o_dll->instance, lbl);
           if (sym != NULL) {
	     /*fprintf(stderr, "found %s in %s\n", lbl,o_dll->name); fflush(stderr);*/
	     return sym;
	   }
        }
        return NULL;
#       else
        ASSERT(2+2 == 5);
        return NULL;
#       endif
    } else {
	return val;
    }
}

static 
__attribute((unused))
void *
lookupLocalSymbol( ObjectCode* oc, char *lbl )
{
    void *val;
    val = lookupStrHashTable(oc->lochash, lbl);

    if (val == NULL) {
        return NULL;
    } else {
	return val;
    }
}


/* -----------------------------------------------------------------------------
 * Debugging aid: look in GHCi's object symbol tables for symbols
 * within DELTA bytes of the specified address, and show their names.
 */
#ifdef DEBUG
void ghci_enquire ( char* addr );

void ghci_enquire ( char* addr )
{
   int   i;
   char* sym;
   char* a;
   const int DELTA = 64;
   ObjectCode* oc;
   for (oc = objects; oc; oc = oc->next) {
      for (i = 0; i < oc->n_symbols; i++) {
         sym = oc->symbols[i];
         if (sym == NULL) continue;
         /* fprintf(stderr, "enquire %p %p\n", sym, oc->lochash); */
         a = NULL;
         if (oc->lochash != NULL)
            a = lookupStrHashTable(oc->lochash, sym);
         if (a == NULL)
            a = lookupStrHashTable(symhash, sym);
         if (a == NULL) {
            /* fprintf(stderr, "ghci_enquire: can't find %s\n", sym); */
         } 
         else if (addr-DELTA <= a && a <= addr+DELTA) {
            fprintf(stderr, "%p + %3d  ==  `%s'\n", addr, a - addr, sym);
         }
      }
   }
}
#endif


/* -----------------------------------------------------------------------------
 * Load an obj (populate the global symbol table, but don't resolve yet)
 *
 * Returns: 1 if ok, 0 on error.
 */
HsInt
loadObj( char *path )
{
   ObjectCode* oc;
   struct stat st;
   int r, n;
   FILE *f;

   /* fprintf(stderr, "loadObj %s\n", path ); */

   /* Check that we haven't already loaded this object.  Don't give up
      at this stage; ocGetNames_* will barf later. */
   { 
       ObjectCode *o;
       int is_dup = 0;
       for (o = objects; o; o = o->next) {
          if (0 == strcmp(o->fileName, path))
             is_dup = 1;
       }
       if (is_dup) {
	 fprintf(stderr, 
            "\n\n"
            "GHCi runtime linker: warning: looks like you're trying to load the\n"
            "same object file twice:\n"
            "   %s\n"
            "GHCi will continue, but a duplicate-symbol error may shortly follow.\n"
            "\n"
            , path);
       }
   }

   oc = stgMallocBytes(sizeof(ObjectCode), "loadObj(oc)");

#  if defined(OBJFORMAT_ELF)
   oc->formatName = "ELF";
#  elif defined(OBJFORMAT_PEi386)
   oc->formatName = "PEi386";
#  else
   free(oc);
   barf("loadObj: not implemented on this platform");
#  endif

   r = stat(path, &st);
   if (r == -1) { return 0; }

   /* sigh, strdup() isn't a POSIX function, so do it the long way */
   oc->fileName = stgMallocBytes( strlen(path)+1, "loadObj" );
   strcpy(oc->fileName, path);

   oc->fileSize          = st.st_size;
   oc->image             = stgMallocBytes( st.st_size, "loadObj(image)" );
   oc->symbols           = NULL;
   oc->sections          = NULL;
   oc->lochash           = allocStrHashTable();
   oc->proddables        = NULL;

   /* chain it onto the list of objects */
   oc->next              = objects;
   objects               = oc;

   /* load the image into memory */
   f = fopen(path, "rb");
   if (!f) {
       barf("loadObj: can't read `%s'", path);
   }
   n = fread ( oc->image, 1, oc->fileSize, f );
   if (n != oc->fileSize) {
      fclose(f);
      barf("loadObj: error whilst reading `%s'", path);
   }

   /* verify the in-memory image */
#  if defined(OBJFORMAT_ELF)
   r = ocVerifyImage_ELF ( oc );
#  elif defined(OBJFORMAT_PEi386)
   r = ocVerifyImage_PEi386 ( oc );
#  else
   barf("loadObj: no verify method");
#  endif
   if (!r) { return r; }

   /* build the symbol list for this image */
#  if defined(OBJFORMAT_ELF)
   r = ocGetNames_ELF ( oc );
#  elif defined(OBJFORMAT_PEi386)
   r = ocGetNames_PEi386 ( oc );
#  else
   barf("loadObj: no getNames method");
#  endif
   if (!r) { return r; }

   /* loaded, but not resolved yet */
   oc->status = OBJECT_LOADED;

   return 1;
}

/* -----------------------------------------------------------------------------
 * resolve all the currently unlinked objects in memory
 *
 * Returns: 1 if ok, 0 on error.
 */
HsInt 
resolveObjs( void )
{
    ObjectCode *oc;
    int r;

    for (oc = objects; oc; oc = oc->next) {
	if (oc->status != OBJECT_RESOLVED) {
#           if defined(OBJFORMAT_ELF)
	    r = ocResolve_ELF ( oc );
#           elif defined(OBJFORMAT_PEi386)
	    r = ocResolve_PEi386 ( oc );
#           else
	    barf("resolveObjs: not implemented on this platform");
#           endif
	    if (!r) { return r; }
	    oc->status = OBJECT_RESOLVED;
	}
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * delete an object from the pool
 */
HsInt
unloadObj( char *path )
{
    ObjectCode *oc, *prev;

    ASSERT(symhash != NULL);
    ASSERT(objects != NULL);

    prev = NULL;
    for (oc = objects; oc; prev = oc, oc = oc->next) {
	if (!strcmp(oc->fileName,path)) {

	    /* Remove all the mappings for the symbols within this
	     * object..
	     */
	    { 
                int i;
                for (i = 0; i < oc->n_symbols; i++) {
                   if (oc->symbols[i] != NULL) {
                       removeStrHashTable(symhash, oc->symbols[i], NULL);
                   }
                }
            }

	    if (prev == NULL) {
		objects = oc->next;
	    } else {
		prev->next = oc->next;
	    }

	    /* We're going to leave this in place, in case there are
	       any pointers from the heap into it: */
	    /* free(oc->image); */
	    free(oc->fileName);
	    free(oc->symbols);
	    free(oc->sections);
	    /* The local hash table should have been freed at the end
               of the ocResolve_ call on it. */
            ASSERT(oc->lochash == NULL);
	    free(oc);
	    return 1;
	}
    }

    belch("unloadObj: can't find `%s' to unload", path);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Sanity checking.  For each ObjectCode, maintain a list of address ranges
 * which may be prodded during relocation, and abort if we try and write
 * outside any of these.
 */
static void addProddableBlock ( ObjectCode* oc, void* start, int size )
{
   ProddableBlock* pb 
      = stgMallocBytes(sizeof(ProddableBlock), "addProddableBlock");
   /* fprintf(stderr, "aPB %p %p %d\n", oc, start, size); */
   ASSERT(size > 0);
   pb->start      = start;
   pb->size       = size;
   pb->next       = oc->proddables;
   oc->proddables = pb;
}

static void checkProddableBlock ( ObjectCode* oc, void* addr )
{
   ProddableBlock* pb;
   for (pb = oc->proddables; pb != NULL; pb = pb->next) {
      char* s = (char*)(pb->start);
      char* e = s + pb->size - 1;
      char* a = (char*)addr;
      /* Assumes that the biggest fixup involves a 4-byte write.  This
         probably needs to be changed to 8 (ie, +7) on 64-bit
         plats. */
      if (a >= s && (a+3) <= e) return;
   }
   barf("checkProddableBlock: invalid fixup in runtime linker");
}

/* -----------------------------------------------------------------------------
 * Section management.
 */
static void addSection ( ObjectCode* oc, SectionKind kind,
                         void* start, void* end )
{
   Section* s   = stgMallocBytes(sizeof(Section), "addSection");
   s->start     = start;
   s->end       = end;
   s->kind      = kind;
   s->next      = oc->sections;
   oc->sections = s;
   /* 
   fprintf(stderr, "addSection: %p-%p (size %d), kind %d\n", 
                   start, ((char*)end)-1, end - start + 1, kind ); 
   */
}



/* --------------------------------------------------------------------------
 * PEi386 specifics (Win32 targets)
 * ------------------------------------------------------------------------*/

/* The information for this linker comes from 
      Microsoft Portable Executable 
      and Common Object File Format Specification
      revision 5.1 January 1998
   which SimonM says comes from the MS Developer Network CDs.
   
   It can be found there (on older CDs), but can also be found 
   online at:

      http://www.microsoft.com/hwdev/hardware/PECOFF.asp

   (this is Rev 6.0 from February 1999).

   Things move, so if that fails, try searching for it via

      http://www.google.com/search?q=PE+COFF+specification     

   The ultimate reference for the PE format is the Winnt.h 
   header file that comes with the Platform SDKs; as always,
   implementations will drift wrt their documentation.
   
   A good background article on the PE format is Matt Pietrek's
   March 1994 article in Microsoft System Journal (MSJ)
   (Vol.9, No. 3): "Peering Inside the PE: A Tour of the
   Win32 Portable Executable File Format." The info in there
   has recently been updated in a two part article in 
   MSDN magazine, issues Feb and March 2002,
   "Inside Windows: An In-Depth Look into the Win32 Portable
   Executable File Format"

   John Levine's book "Linkers and Loaders" contains useful
   info on PE too.
*/
      

#if defined(OBJFORMAT_PEi386)



typedef unsigned char  UChar;
typedef unsigned short UInt16;
typedef unsigned int   UInt32;
typedef          int   Int32;


typedef 
   struct {
      UInt16 Machine;
      UInt16 NumberOfSections;
      UInt32 TimeDateStamp;
      UInt32 PointerToSymbolTable;
      UInt32 NumberOfSymbols;
      UInt16 SizeOfOptionalHeader;
      UInt16 Characteristics;
   }
   COFF_header;

#define sizeof_COFF_header 20


typedef 
   struct {
      UChar  Name[8];
      UInt32 VirtualSize;
      UInt32 VirtualAddress;
      UInt32 SizeOfRawData;
      UInt32 PointerToRawData;
      UInt32 PointerToRelocations;
      UInt32 PointerToLinenumbers;
      UInt16 NumberOfRelocations;
      UInt16 NumberOfLineNumbers;
      UInt32 Characteristics; 
   }
   COFF_section;

#define sizeof_COFF_section 40


typedef
   struct {
      UChar  Name[8];
      UInt32 Value;
      UInt16 SectionNumber;
      UInt16 Type;
      UChar  StorageClass;
      UChar  NumberOfAuxSymbols;
   }
   COFF_symbol;

#define sizeof_COFF_symbol 18


typedef
   struct {
      UInt32 VirtualAddress;
      UInt32 SymbolTableIndex;
      UInt16 Type;
   }
   COFF_reloc;

#define sizeof_COFF_reloc 10


/* From PE spec doc, section 3.3.2 */
/* Note use of MYIMAGE_* since IMAGE_* are already defined in
   windows.h -- for the same purpose, but I want to know what I'm
   getting, here. */
#define MYIMAGE_FILE_RELOCS_STRIPPED     0x0001
#define MYIMAGE_FILE_EXECUTABLE_IMAGE    0x0002
#define MYIMAGE_FILE_DLL                 0x2000
#define MYIMAGE_FILE_SYSTEM              0x1000
#define MYIMAGE_FILE_BYTES_REVERSED_HI   0x8000
#define MYIMAGE_FILE_BYTES_REVERSED_LO   0x0080
#define MYIMAGE_FILE_32BIT_MACHINE       0x0100

/* From PE spec doc, section 5.4.2 and 5.4.4 */
#define MYIMAGE_SYM_CLASS_EXTERNAL       2
#define MYIMAGE_SYM_CLASS_STATIC         3
#define MYIMAGE_SYM_UNDEFINED            0

/* From PE spec doc, section 4.1 */
#define MYIMAGE_SCN_CNT_CODE             0x00000020
#define MYIMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define MYIMAGE_SCN_LNK_NRELOC_OVFL      0x01000000

/* From PE spec doc, section 5.2.1 */
#define MYIMAGE_REL_I386_DIR32           0x0006
#define MYIMAGE_REL_I386_REL32           0x0014


/* We use myindex to calculate array addresses, rather than
   simply doing the normal subscript thing.  That's because
   some of the above structs have sizes which are not 
   a whole number of words.  GCC rounds their sizes up to a
   whole number of words, which means that the address calcs
   arising from using normal C indexing or pointer arithmetic
   are just plain wrong.  Sigh.
*/
static UChar *
myindex ( int scale, void* base, int index )
{
   return
      ((UChar*)base) + scale * index;
}


static void
printName ( UChar* name, UChar* strtab )
{
   if (name[0]==0 && name[1]==0 && name[2]==0 && name[3]==0) {
      UInt32 strtab_offset = * (UInt32*)(name+4);
      fprintf ( stderr, "%s", strtab + strtab_offset );
   } else {
      int i;
      for (i = 0; i < 8; i++) {
         if (name[i] == 0) break;
         fprintf ( stderr, "%c", name[i] );
      }
   }
}


static void
copyName ( UChar* name, UChar* strtab, UChar* dst, int dstSize )
{
   if (name[0]==0 && name[1]==0 && name[2]==0 && name[3]==0) {
      UInt32 strtab_offset = * (UInt32*)(name+4);
      strncpy ( dst, strtab+strtab_offset, dstSize );
      dst[dstSize-1] = 0;
   } else {
      int i = 0;
      while (1) {
         if (i >= 8) break;
         if (name[i] == 0) break;
         dst[i] = name[i];
         i++;
      }
      dst[i] = 0;
   }
}


static UChar *
cstring_from_COFF_symbol_name ( UChar* name, UChar* strtab )
{
   UChar* newstr;
   /* If the string is longer than 8 bytes, look in the
      string table for it -- this will be correctly zero terminated. 
   */
   if (name[0]==0 && name[1]==0 && name[2]==0 && name[3]==0) {
      UInt32 strtab_offset = * (UInt32*)(name+4);
      return ((UChar*)strtab) + strtab_offset;
   }
   /* Otherwise, if shorter than 8 bytes, return the original,
      which by defn is correctly terminated.
   */
   if (name[7]==0) return name;
   /* The annoying case: 8 bytes.  Copy into a temporary
      (which is never freed ...)
   */
   newstr = stgMallocBytes(9, "cstring_from_COFF_symbol_name");
   ASSERT(newstr);
   strncpy(newstr,name,8);
   newstr[8] = 0;
   return newstr;
}


/* Just compares the short names (first 8 chars) */
static COFF_section *
findPEi386SectionCalled ( ObjectCode* oc,  char* name )
{
   int i;
   COFF_header* hdr 
      = (COFF_header*)(oc->image);
   COFF_section* sectab 
      = (COFF_section*) (
           ((UChar*)(oc->image)) 
           + sizeof_COFF_header + hdr->SizeOfOptionalHeader
        );
   for (i = 0; i < hdr->NumberOfSections; i++) {
      UChar* n1;
      UChar* n2;
      COFF_section* section_i 
         = (COFF_section*)
           myindex ( sizeof_COFF_section, sectab, i );
      n1 = (UChar*) &(section_i->Name);
      n2 = name;
      if (n1[0]==n2[0] && n1[1]==n2[1] && n1[2]==n2[2] && 
          n1[3]==n2[3] && n1[4]==n2[4] && n1[5]==n2[5] && 
          n1[6]==n2[6] && n1[7]==n2[7])
         return section_i;
   }

   return NULL;
}


static void
zapTrailingAtSign ( UChar* sym )
{
#  define my_isdigit(c) ((c) >= '0' && (c) <= '9')
   int i, j;
   if (sym[0] == 0) return;
   i = 0; 
   while (sym[i] != 0) i++;
   i--;
   j = i;
   while (j > 0 && my_isdigit(sym[j])) j--;
   if (j > 0 && sym[j] == '@' && j != i) sym[j] = 0;
#  undef my_isdigit
}


static int
ocVerifyImage_PEi386 ( ObjectCode* oc )
{
   int i;
   UInt32 j, noRelocs;
   COFF_header*  hdr;
   COFF_section* sectab;
   COFF_symbol*  symtab;
   UChar*        strtab;
   /* fprintf(stderr, "\nLOADING %s\n", oc->fileName); */
   hdr = (COFF_header*)(oc->image);
   sectab = (COFF_section*) (
               ((UChar*)(oc->image)) 
               + sizeof_COFF_header + hdr->SizeOfOptionalHeader
            );
   symtab = (COFF_symbol*) (
               ((UChar*)(oc->image))
               + hdr->PointerToSymbolTable 
            );
   strtab = ((UChar*)symtab)
            + hdr->NumberOfSymbols * sizeof_COFF_symbol;

   if (hdr->Machine != 0x14c) {
      belch("Not x86 PEi386");
      return 0;
   }
   if (hdr->SizeOfOptionalHeader != 0) {
      belch("PEi386 with nonempty optional header");
      return 0;
   }
   if ( /* (hdr->Characteristics & MYIMAGE_FILE_RELOCS_STRIPPED) || */
        (hdr->Characteristics & MYIMAGE_FILE_EXECUTABLE_IMAGE) ||
        (hdr->Characteristics & MYIMAGE_FILE_DLL) ||
        (hdr->Characteristics & MYIMAGE_FILE_SYSTEM) ) {
      belch("Not a PEi386 object file");
      return 0;
   }
   if ( (hdr->Characteristics & MYIMAGE_FILE_BYTES_REVERSED_HI)
        /* || !(hdr->Characteristics & MYIMAGE_FILE_32BIT_MACHINE) */ ) {
      belch("Invalid PEi386 word size or endiannness: %d", 
            (int)(hdr->Characteristics));
      return 0;
   }
   /* If the string table size is way crazy, this might indicate that
      there are more than 64k relocations, despite claims to the
      contrary.  Hence this test. */
   /* fprintf(stderr, "strtab size %d\n", * (UInt32*)strtab); */
#if 0
   if ( (*(UInt32*)strtab) > 600000 ) {
      /* Note that 600k has no special significance other than being
         big enough to handle the almost-2MB-sized lumps that
         constitute HSwin32*.o. */
      belch("PEi386 object has suspiciously large string table; > 64k relocs?");
      return 0;
   }
#endif

   /* No further verification after this point; only debug printing. */
   i = 0;
   IF_DEBUG(linker, i=1);
   if (i == 0) return 1;

   fprintf ( stderr, 
             "sectab offset = %d\n", ((UChar*)sectab) - ((UChar*)hdr) );
   fprintf ( stderr, 
             "symtab offset = %d\n", ((UChar*)symtab) - ((UChar*)hdr) );
   fprintf ( stderr, 
             "strtab offset = %d\n", ((UChar*)strtab) - ((UChar*)hdr) );

   fprintf ( stderr, "\n" );
   fprintf ( stderr, 
             "Machine:           0x%x\n", (UInt32)(hdr->Machine) );
   fprintf ( stderr, 
             "# sections:        %d\n",   (UInt32)(hdr->NumberOfSections) );
   fprintf ( stderr,
             "time/date:         0x%x\n", (UInt32)(hdr->TimeDateStamp) );
   fprintf ( stderr,
             "symtab offset:     %d\n",   (UInt32)(hdr->PointerToSymbolTable) );
   fprintf ( stderr, 
             "# symbols:         %d\n",   (UInt32)(hdr->NumberOfSymbols) );
   fprintf ( stderr, 
             "sz of opt hdr:     %d\n",   (UInt32)(hdr->SizeOfOptionalHeader) );
   fprintf ( stderr,
             "characteristics:   0x%x\n", (UInt32)(hdr->Characteristics) );

   /* Print the section table. */
   fprintf ( stderr, "\n" );
   for (i = 0; i < hdr->NumberOfSections; i++) {
      COFF_reloc* reltab;
      COFF_section* sectab_i
         = (COFF_section*)
           myindex ( sizeof_COFF_section, sectab, i );
      fprintf ( stderr, 
                "\n"
                "section %d\n"
                "     name `",
                i 
              );
      printName ( sectab_i->Name, strtab );
      fprintf ( stderr, 
                "'\n"
                "    vsize %d\n"
                "    vaddr %d\n"
                "  data sz %d\n"
                " data off %d\n"
                "  num rel %d\n"
                "  off rel %d\n"
                "  ptr raw 0x%x\n",
                sectab_i->VirtualSize,
                sectab_i->VirtualAddress,
                sectab_i->SizeOfRawData,
                sectab_i->PointerToRawData,
                sectab_i->NumberOfRelocations,
                sectab_i->PointerToRelocations,
                sectab_i->PointerToRawData
              );
      reltab = (COFF_reloc*) (
                  ((UChar*)(oc->image)) + sectab_i->PointerToRelocations
               );
	       
      if ( sectab_i->Characteristics & MYIMAGE_SCN_LNK_NRELOC_OVFL ) {
	/* If the relocation field (a short) has overflowed, the
	 * real count can be found in the first reloc entry.
	 * 
	 * See Section 4.1 (last para) of the PE spec (rev6.0).
	 */
        COFF_reloc* rel = (COFF_reloc*)
                           myindex ( sizeof_COFF_reloc, reltab, 0 );
	noRelocs = rel->VirtualAddress;
	j = 1;
      } else {
	noRelocs = sectab_i->NumberOfRelocations;
        j = 0;
      }

      for (; j < noRelocs; j++) {
         COFF_symbol* sym;
         COFF_reloc* rel = (COFF_reloc*)
                           myindex ( sizeof_COFF_reloc, reltab, j );
         fprintf ( stderr, 
                   "        type 0x%-4x   vaddr 0x%-8x   name `",
                   (UInt32)rel->Type, 
                   rel->VirtualAddress );
         sym = (COFF_symbol*)
               myindex ( sizeof_COFF_symbol, symtab, rel->SymbolTableIndex );
	 /* Hmm..mysterious looking offset - what's it for? SOF */
         printName ( sym->Name, strtab -10 );
         fprintf ( stderr, "'\n" );
      }

      fprintf ( stderr, "\n" );
   }
   fprintf ( stderr, "\n" );
   fprintf ( stderr, "string table has size 0x%x\n", * (UInt32*)strtab );
   fprintf ( stderr, "---START of string table---\n");
   for (i = 4; i < *(Int32*)strtab; i++) {
      if (strtab[i] == 0) 
         fprintf ( stderr, "\n"); else 
         fprintf( stderr, "%c", strtab[i] );
   }
   fprintf ( stderr, "--- END  of string table---\n");

   fprintf ( stderr, "\n" );
   i = 0;
   while (1) {
      COFF_symbol* symtab_i;
      if (i >= (Int32)(hdr->NumberOfSymbols)) break;
      symtab_i = (COFF_symbol*)
                 myindex ( sizeof_COFF_symbol, symtab, i );
      fprintf ( stderr, 
                "symbol %d\n"
                "     name `",
                i 
              );
      printName ( symtab_i->Name, strtab );
      fprintf ( stderr, 
                "'\n"
                "    value 0x%x\n"
                "   1+sec# %d\n"
                "     type 0x%x\n"
                "   sclass 0x%x\n"
                "     nAux %d\n",
                symtab_i->Value,
                (Int32)(symtab_i->SectionNumber),
                (UInt32)symtab_i->Type,
                (UInt32)symtab_i->StorageClass,
                (UInt32)symtab_i->NumberOfAuxSymbols 
              );
      i += symtab_i->NumberOfAuxSymbols;
      i++;
   }

   fprintf ( stderr, "\n" );
   return 1;
}


static int
ocGetNames_PEi386 ( ObjectCode* oc )
{
   COFF_header*  hdr;
   COFF_section* sectab;
   COFF_symbol*  symtab;
   UChar*        strtab;

   UChar* sname;
   void*  addr;
   int    i;
   
   hdr = (COFF_header*)(oc->image);
   sectab = (COFF_section*) (
               ((UChar*)(oc->image)) 
               + sizeof_COFF_header + hdr->SizeOfOptionalHeader
            );
   symtab = (COFF_symbol*) (
               ((UChar*)(oc->image))
               + hdr->PointerToSymbolTable 
            );
   strtab = ((UChar*)(oc->image))
            + hdr->PointerToSymbolTable
            + hdr->NumberOfSymbols * sizeof_COFF_symbol;

   /* Allocate space for any (local, anonymous) .bss sections. */

   for (i = 0; i < hdr->NumberOfSections; i++) {
      UChar* zspace;
      COFF_section* sectab_i
         = (COFF_section*)
           myindex ( sizeof_COFF_section, sectab, i );
      if (0 != strcmp(sectab_i->Name, ".bss")) continue;
      if (sectab_i->VirtualSize == 0) continue;
      /* This is a non-empty .bss section.  Allocate zeroed space for
         it, and set its PointerToRawData field such that oc->image +
         PointerToRawData == addr_of_zeroed_space.  */
      zspace = stgCallocBytes(1, sectab_i->VirtualSize, 
                              "ocGetNames_PEi386(anonymous bss)");
      sectab_i->PointerToRawData = ((UChar*)zspace) - ((UChar*)(oc->image));
      addProddableBlock(oc, zspace, sectab_i->VirtualSize);
      /* fprintf(stderr, "BSS anon section at 0x%x\n", zspace); */
   }

   /* Copy section information into the ObjectCode. */

   for (i = 0; i < hdr->NumberOfSections; i++) {
      UChar* start;
      UChar* end;
      UInt32 sz;

      SectionKind kind 
         = SECTIONKIND_OTHER;
      COFF_section* sectab_i
         = (COFF_section*)
           myindex ( sizeof_COFF_section, sectab, i );
      IF_DEBUG(linker, belch("section name = %s\n", sectab_i->Name ));

#     if 0
      /* I'm sure this is the Right Way to do it.  However, the 
         alternative of testing the sectab_i->Name field seems to
         work ok with Cygwin.
      */
      if (sectab_i->Characteristics & MYIMAGE_SCN_CNT_CODE || 
          sectab_i->Characteristics & MYIMAGE_SCN_CNT_INITIALIZED_DATA)
         kind = SECTIONKIND_CODE_OR_RODATA;
#     endif

      if (0==strcmp(".text",sectab_i->Name) ||
          0==strcmp(".rodata",sectab_i->Name))
         kind = SECTIONKIND_CODE_OR_RODATA;
      if (0==strcmp(".data",sectab_i->Name) ||
          0==strcmp(".bss",sectab_i->Name))
         kind = SECTIONKIND_RWDATA;

      ASSERT(sectab_i->SizeOfRawData == 0 || sectab_i->VirtualSize == 0);
      sz = sectab_i->SizeOfRawData;
      if (sz < sectab_i->VirtualSize) sz = sectab_i->VirtualSize;

      start = ((UChar*)(oc->image)) + sectab_i->PointerToRawData;
      end   = start + sz - 1;

      if (kind == SECTIONKIND_OTHER
          /* Ignore sections called which contain stabs debugging
             information. */
          && 0 != strcmp(".stab", sectab_i->Name)
          && 0 != strcmp(".stabstr", sectab_i->Name)
         ) {
         belch("Unknown PEi386 section name `%s'", sectab_i->Name);
         return 0;
      }

      if (kind != SECTIONKIND_OTHER && end >= start) {
         addSection(oc, kind, start, end);
         addProddableBlock(oc, start, end - start + 1);
      }
   }

   /* Copy exported symbols into the ObjectCode. */

   oc->n_symbols = hdr->NumberOfSymbols;
   oc->symbols   = stgMallocBytes(oc->n_symbols * sizeof(char*),
                                  "ocGetNames_PEi386(oc->symbols)");
   /* Call me paranoid; I don't care. */
   for (i = 0; i < oc->n_symbols; i++) 
      oc->symbols[i] = NULL;

   i = 0;
   while (1) {
      COFF_symbol* symtab_i;
      if (i >= (Int32)(hdr->NumberOfSymbols)) break;
      symtab_i = (COFF_symbol*)
                 myindex ( sizeof_COFF_symbol, symtab, i );

      addr  = NULL;

      if (symtab_i->StorageClass == MYIMAGE_SYM_CLASS_EXTERNAL
          && symtab_i->SectionNumber != MYIMAGE_SYM_UNDEFINED) {
         /* This symbol is global and defined, viz, exported */
         /* for MYIMAGE_SYMCLASS_EXTERNAL 
                && !MYIMAGE_SYM_UNDEFINED,
            the address of the symbol is: 
                address of relevant section + offset in section
         */
         COFF_section* sectabent 
            = (COFF_section*) myindex ( sizeof_COFF_section, 
                                        sectab,
                                        symtab_i->SectionNumber-1 );
         addr = ((UChar*)(oc->image))
                + (sectabent->PointerToRawData
                   + symtab_i->Value);
      } 
      else
      if (symtab_i->SectionNumber == MYIMAGE_SYM_UNDEFINED
	  && symtab_i->Value > 0) {
         /* This symbol isn't in any section at all, ie, global bss.
            Allocate zeroed space for it. */
         addr = stgCallocBytes(1, symtab_i->Value, 
                               "ocGetNames_PEi386(non-anonymous bss)");
         addSection(oc, SECTIONKIND_RWDATA, addr, 
                        ((UChar*)addr) + symtab_i->Value - 1);
         addProddableBlock(oc, addr, symtab_i->Value);
         /* fprintf(stderr, "BSS      section at 0x%x\n", addr); */
      }

      if (addr != NULL ) {
         sname = cstring_from_COFF_symbol_name ( symtab_i->Name, strtab );
         /* fprintf(stderr,"addSymbol %p `%s \n", addr,sname);  */
         IF_DEBUG(linker, belch("addSymbol %p `%s'\n", addr,sname);)
         ASSERT(i >= 0 && i < oc->n_symbols);
         /* cstring_from_COFF_symbol_name always succeeds. */
         oc->symbols[i] = sname;
         ghciInsertStrHashTable(oc->fileName, symhash, sname, addr);
      } else {
#        if 0
         fprintf ( stderr, 
                   "IGNORING symbol %d\n"
                   "     name `",
                   i 
                 );
         printName ( symtab_i->Name, strtab );
         fprintf ( stderr, 
                   "'\n"
                   "    value 0x%x\n"
                   "   1+sec# %d\n"
                   "     type 0x%x\n"
                   "   sclass 0x%x\n"
                   "     nAux %d\n",
                   symtab_i->Value,
                   (Int32)(symtab_i->SectionNumber),
                   (UInt32)symtab_i->Type,
                   (UInt32)symtab_i->StorageClass,
                   (UInt32)symtab_i->NumberOfAuxSymbols 
                 );
#        endif
      }

      i += symtab_i->NumberOfAuxSymbols;
      i++;
   }

   return 1;   
}


static int
ocResolve_PEi386 ( ObjectCode* oc )
{
   COFF_header*  hdr;
   COFF_section* sectab;
   COFF_symbol*  symtab;
   UChar*        strtab;

   UInt32        A;
   UInt32        S;
   UInt32*       pP;

   int i;
   UInt32 j, noRelocs;

   /* ToDo: should be variable-sized?  But is at least safe in the
      sense of buffer-overrun-proof. */
   char symbol[1000];
   /* fprintf(stderr, "resolving for %s\n", oc->fileName); */

   hdr = (COFF_header*)(oc->image);
   sectab = (COFF_section*) (
               ((UChar*)(oc->image)) 
               + sizeof_COFF_header + hdr->SizeOfOptionalHeader
            );
   symtab = (COFF_symbol*) (
               ((UChar*)(oc->image))
               + hdr->PointerToSymbolTable 
            );
   strtab = ((UChar*)(oc->image))
            + hdr->PointerToSymbolTable
            + hdr->NumberOfSymbols * sizeof_COFF_symbol;

   for (i = 0; i < hdr->NumberOfSections; i++) {
      COFF_section* sectab_i
         = (COFF_section*)
           myindex ( sizeof_COFF_section, sectab, i );
      COFF_reloc* reltab
         = (COFF_reloc*) (
              ((UChar*)(oc->image)) + sectab_i->PointerToRelocations
           );

      /* Ignore sections called which contain stabs debugging
         information. */
      if (0 == strcmp(".stab", sectab_i->Name)
          || 0 == strcmp(".stabstr", sectab_i->Name))
         continue;

      if ( sectab_i->Characteristics & MYIMAGE_SCN_LNK_NRELOC_OVFL ) {
	/* If the relocation field (a short) has overflowed, the
	 * real count can be found in the first reloc entry.
         *
	 * See Section 4.1 (last para) of the PE spec (rev6.0).
	 */
        COFF_reloc* rel = (COFF_reloc*)
                           myindex ( sizeof_COFF_reloc, reltab, 0 );
	noRelocs = rel->VirtualAddress;
	fprintf(stderr, "Overflown relocs: %u\n", noRelocs);
	j = 1;
      } else {
	noRelocs = sectab_i->NumberOfRelocations;
        j = 0;
      }


      for (; j < noRelocs; j++) {
         COFF_symbol* sym;
         COFF_reloc* reltab_j 
            = (COFF_reloc*)
              myindex ( sizeof_COFF_reloc, reltab, j );

         /* the location to patch */
         pP = (UInt32*)(
                 ((UChar*)(oc->image)) 
                 + (sectab_i->PointerToRawData 
                    + reltab_j->VirtualAddress
                    - sectab_i->VirtualAddress )
              );
         /* the existing contents of pP */
         A = *pP;
         /* the symbol to connect to */
         sym = (COFF_symbol*)
               myindex ( sizeof_COFF_symbol, 
                         symtab, reltab_j->SymbolTableIndex );
         IF_DEBUG(linker,
                  fprintf ( stderr, 
                            "reloc sec %2d num %3d:  type 0x%-4x   "
                            "vaddr 0x%-8x   name `",
                            i, j,
                            (UInt32)reltab_j->Type, 
                            reltab_j->VirtualAddress );
                            printName ( sym->Name, strtab );
                            fprintf ( stderr, "'\n" ));

         if (sym->StorageClass == MYIMAGE_SYM_CLASS_STATIC) {
            COFF_section* section_sym 
               = findPEi386SectionCalled ( oc, sym->Name );
            if (!section_sym) {
               belch("%s: can't find section `%s'", oc->fileName, sym->Name);
               return 0;
            }
            S = ((UInt32)(oc->image))
                + (section_sym->PointerToRawData
                   + sym->Value);
         } else {
            copyName ( sym->Name, strtab, symbol, 1000-1 );
            (void*)S = lookupLocalSymbol( oc, symbol );
            if ((void*)S != NULL) goto foundit;
            (void*)S = lookupSymbol( symbol );
            if ((void*)S != NULL) goto foundit;
            zapTrailingAtSign ( symbol );
            (void*)S = lookupLocalSymbol( oc, symbol );
            if ((void*)S != NULL) goto foundit;
            (void*)S = lookupSymbol( symbol );
            if ((void*)S != NULL) goto foundit;
            belch("%s: unknown symbol `%s'", oc->fileName, symbol);
            return 0;
           foundit:
         }
         checkProddableBlock(oc, pP);
         switch (reltab_j->Type) {
            case MYIMAGE_REL_I386_DIR32: 
               *pP = A + S; 
               break;
            case MYIMAGE_REL_I386_REL32:
               /* Tricky.  We have to insert a displacement at
                  pP which, when added to the PC for the _next_
                  insn, gives the address of the target (S).
                  Problem is to know the address of the next insn
                  when we only know pP.  We assume that this
                  literal field is always the last in the insn,
                  so that the address of the next insn is pP+4
                  -- hence the constant 4.
                  Also I don't know if A should be added, but so
                  far it has always been zero.
	       */
               ASSERT(A==0);
               *pP = S - ((UInt32)pP) - 4;
               break;
            default: 
               belch("%s: unhandled PEi386 relocation type %d", 
		     oc->fileName, reltab_j->Type);
               return 0;
         }

      }
   }
   
   IF_DEBUG(linker, belch("completed %s", oc->fileName));
   return 1;
}

#endif /* defined(OBJFORMAT_PEi386) */


/* --------------------------------------------------------------------------
 * ELF specifics
 * ------------------------------------------------------------------------*/

#if defined(OBJFORMAT_ELF)

#define FALSE 0
#define TRUE  1

#if defined(sparc_TARGET_ARCH)
#  define ELF_TARGET_SPARC  /* Used inside <elf.h> */
#elif defined(i386_TARGET_ARCH)
#  define ELF_TARGET_386    /* Used inside <elf.h> */
#endif
/* There is a similar case for IA64 in the Solaris2 headers if this
 * ever becomes relevant.
 */

#include <elf.h>
#include <ctype.h>

static char *
findElfSection ( void* objImage, Elf32_Word sh_type )
{
   int i;
   char* ehdrC = (char*)objImage;
   Elf32_Ehdr* ehdr = (Elf32_Ehdr*)ehdrC;
   Elf32_Shdr* shdr = (Elf32_Shdr*)(ehdrC + ehdr->e_shoff);
   char* sh_strtab = ehdrC + shdr[ehdr->e_shstrndx].sh_offset;
   char* ptr = NULL;
   for (i = 0; i < ehdr->e_shnum; i++) {
      if (shdr[i].sh_type == sh_type
          /* Ignore the section header's string table. */
          && i != ehdr->e_shstrndx
	  /* Ignore string tables named .stabstr, as they contain
             debugging info. */
          && 0 != strncmp(".stabstr", sh_strtab + shdr[i].sh_name, 8)
         ) {
         ptr = ehdrC + shdr[i].sh_offset;
         break;
      }
   }
   return ptr;
}


static int
ocVerifyImage_ELF ( ObjectCode* oc )
{
   Elf32_Shdr* shdr;
   Elf32_Sym*  stab;
   int i, j, nent, nstrtab, nsymtabs;
   char* sh_strtab;
   char* strtab;

   char*       ehdrC = (char*)(oc->image);
   Elf32_Ehdr* ehdr  = ( Elf32_Ehdr*)ehdrC;

   if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
       ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
       ehdr->e_ident[EI_MAG3] != ELFMAG3) {
      belch("%s: not an ELF header", oc->fileName);
      return 0;
   }
   IF_DEBUG(linker,belch( "Is an ELF header" ));

   if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
      belch("%s: not 32 bit ELF", oc->fileName);
      return 0;
   }

   IF_DEBUG(linker,belch( "Is 32 bit ELF" ));

   if (ehdr->e_ident[EI_DATA] == ELFDATA2LSB) {
       IF_DEBUG(linker,belch( "Is little-endian" ));
   } else
   if (ehdr->e_ident[EI_DATA] == ELFDATA2MSB) {
       IF_DEBUG(linker,belch( "Is big-endian" ));
   } else {
       belch("%s: unknown endiannness", oc->fileName);
       return 0;
   }

   if (ehdr->e_type != ET_REL) {
      belch("%s: not a relocatable object (.o) file", oc->fileName);
      return 0;
   }
   IF_DEBUG(linker, belch( "Is a relocatable object (.o) file" ));

   IF_DEBUG(linker,belch( "Architecture is " ));
   switch (ehdr->e_machine) {
      case EM_386:   IF_DEBUG(linker,belch( "x86" )); break;
      case EM_SPARC: IF_DEBUG(linker,belch( "sparc" )); break;
      default:       IF_DEBUG(linker,belch( "unknown" )); 
                     belch("%s: unknown architecture", oc->fileName);
                     return 0;
   }

   IF_DEBUG(linker,belch(
             "\nSection header table: start %d, n_entries %d, ent_size %d", 
             ehdr->e_shoff, ehdr->e_shnum, ehdr->e_shentsize  ));

   ASSERT (ehdr->e_shentsize == sizeof(Elf32_Shdr));

   shdr = (Elf32_Shdr*) (ehdrC + ehdr->e_shoff);

   if (ehdr->e_shstrndx == SHN_UNDEF) {
      belch("%s: no section header string table", oc->fileName);
      return 0;
   } else {
      IF_DEBUG(linker,belch( "Section header string table is section %d", 
                          ehdr->e_shstrndx));
      sh_strtab = ehdrC + shdr[ehdr->e_shstrndx].sh_offset;
   }

   for (i = 0; i < ehdr->e_shnum; i++) {
      IF_DEBUG(linker,fprintf(stderr, "%2d:  ", i ));
      IF_DEBUG(linker,fprintf(stderr, "type=%2d  ", (int)shdr[i].sh_type ));
      IF_DEBUG(linker,fprintf(stderr, "size=%4d  ", (int)shdr[i].sh_size ));
      IF_DEBUG(linker,fprintf(stderr, "offs=%4d  ", (int)shdr[i].sh_offset ));
      IF_DEBUG(linker,fprintf(stderr, "  (%p .. %p)  ",
               ehdrC + shdr[i].sh_offset, 
		      ehdrC + shdr[i].sh_offset + shdr[i].sh_size - 1));

      if (shdr[i].sh_type == SHT_REL) {
	  IF_DEBUG(linker,fprintf(stderr, "Rel  " ));
      } else if (shdr[i].sh_type == SHT_RELA) {
	  IF_DEBUG(linker,fprintf(stderr, "RelA " ));
      } else {
	  IF_DEBUG(linker,fprintf(stderr,"     "));
      }
      if (sh_strtab) {
	  IF_DEBUG(linker,fprintf(stderr, "sname=%s\n", sh_strtab + shdr[i].sh_name ));
      }
   }

   IF_DEBUG(linker,belch( "\nString tables" ));
   strtab = NULL;
   nstrtab = 0;
   for (i = 0; i < ehdr->e_shnum; i++) {
      if (shdr[i].sh_type == SHT_STRTAB
          /* Ignore the section header's string table. */
          && i != ehdr->e_shstrndx
	  /* Ignore string tables named .stabstr, as they contain
             debugging info. */
          && 0 != strncmp(".stabstr", sh_strtab + shdr[i].sh_name, 8)
         ) {
         IF_DEBUG(linker,belch("   section %d is a normal string table", i ));
         strtab = ehdrC + shdr[i].sh_offset;
         nstrtab++;
      }
   }  
   if (nstrtab != 1) {
      belch("%s: no string tables, or too many", oc->fileName);
      return 0;
   }

   nsymtabs = 0;
   IF_DEBUG(linker,belch( "\nSymbol tables" )); 
   for (i = 0; i < ehdr->e_shnum; i++) {
      if (shdr[i].sh_type != SHT_SYMTAB) continue;
      IF_DEBUG(linker,belch( "section %d is a symbol table", i ));
      nsymtabs++;
      stab = (Elf32_Sym*) (ehdrC + shdr[i].sh_offset);
      nent = shdr[i].sh_size / sizeof(Elf32_Sym);
      IF_DEBUG(linker,belch( "   number of entries is apparently %d (%d rem)",
               nent,
               shdr[i].sh_size % sizeof(Elf32_Sym)
             ));
      if (0 != shdr[i].sh_size % sizeof(Elf32_Sym)) {
         belch("%s: non-integral number of symbol table entries", oc->fileName);
         return 0;
      }
      for (j = 0; j < nent; j++) {
         IF_DEBUG(linker,fprintf(stderr, "   %2d  ", j ));
         IF_DEBUG(linker,fprintf(stderr, "  sec=%-5d  size=%-3d  val=%5p  ", 
                             (int)stab[j].st_shndx,
                             (int)stab[j].st_size,
                             (char*)stab[j].st_value ));

         IF_DEBUG(linker,fprintf(stderr, "type=" ));
         switch (ELF32_ST_TYPE(stab[j].st_info)) {
            case STT_NOTYPE:  IF_DEBUG(linker,fprintf(stderr, "notype " )); break;
            case STT_OBJECT:  IF_DEBUG(linker,fprintf(stderr, "object " )); break;
            case STT_FUNC  :  IF_DEBUG(linker,fprintf(stderr, "func   " )); break;
            case STT_SECTION: IF_DEBUG(linker,fprintf(stderr, "section" )); break;
            case STT_FILE:    IF_DEBUG(linker,fprintf(stderr, "file   " )); break;
            default:          IF_DEBUG(linker,fprintf(stderr, "?      " )); break;
         }
         IF_DEBUG(linker,fprintf(stderr, "  " ));

         IF_DEBUG(linker,fprintf(stderr, "bind=" ));
         switch (ELF32_ST_BIND(stab[j].st_info)) {
            case STB_LOCAL :  IF_DEBUG(linker,fprintf(stderr, "local " )); break;
            case STB_GLOBAL:  IF_DEBUG(linker,fprintf(stderr, "global" )); break;
            case STB_WEAK  :  IF_DEBUG(linker,fprintf(stderr, "weak  " )); break;
            default:          IF_DEBUG(linker,fprintf(stderr, "?     " )); break;
         }
         IF_DEBUG(linker,fprintf(stderr, "  " ));

         IF_DEBUG(linker,fprintf(stderr, "name=%s\n", strtab + stab[j].st_name ));
      }
   }

   if (nsymtabs == 0) {
      belch("%s: didn't find any symbol tables", oc->fileName);
      return 0;
   }

   return 1;
}


static int
ocGetNames_ELF ( ObjectCode* oc )
{
   int i, j, k, nent;
   Elf32_Sym* stab;

   char*       ehdrC      = (char*)(oc->image);
   Elf32_Ehdr* ehdr       = (Elf32_Ehdr*)ehdrC;
   char*       strtab     = findElfSection ( ehdrC, SHT_STRTAB );
   Elf32_Shdr* shdr       = (Elf32_Shdr*) (ehdrC + ehdr->e_shoff);

   ASSERT(symhash != NULL);

   if (!strtab) {
      belch("%s: no strtab", oc->fileName);
      return 0;
   }

   k = 0;
   for (i = 0; i < ehdr->e_shnum; i++) {
      /* Figure out what kind of section it is.  Logic derived from
         Figure 1.14 ("Special Sections") of the ELF document
         ("Portable Formats Specification, Version 1.1"). */
      Elf32_Shdr  hdr    = shdr[i];
      SectionKind kind   = SECTIONKIND_OTHER;
      int         is_bss = FALSE;

      if (hdr.sh_type == SHT_PROGBITS 
          && (hdr.sh_flags & SHF_ALLOC) && (hdr.sh_flags & SHF_EXECINSTR)) {
         /* .text-style section */
         kind = SECTIONKIND_CODE_OR_RODATA;
      }
      else
      if (hdr.sh_type == SHT_PROGBITS 
          && (hdr.sh_flags & SHF_ALLOC) && (hdr.sh_flags & SHF_WRITE)) {
         /* .data-style section */
         kind = SECTIONKIND_RWDATA;
      }
      else
      if (hdr.sh_type == SHT_PROGBITS 
          && (hdr.sh_flags & SHF_ALLOC) && !(hdr.sh_flags & SHF_WRITE)) {
         /* .rodata-style section */
         kind = SECTIONKIND_CODE_OR_RODATA;
      }
      else
      if (hdr.sh_type == SHT_NOBITS 
          && (hdr.sh_flags & SHF_ALLOC) && (hdr.sh_flags & SHF_WRITE)) {
         /* .bss-style section */
         kind = SECTIONKIND_RWDATA;
         is_bss = TRUE;
      }

      if (is_bss && shdr[i].sh_size > 0) {
         /* This is a non-empty .bss section.  Allocate zeroed space for
            it, and set its .sh_offset field such that 
            ehdrC + .sh_offset == addr_of_zeroed_space.  */
         char* zspace = stgCallocBytes(1, shdr[i].sh_size, 
                                       "ocGetNames_ELF(BSS)");
         shdr[i].sh_offset = ((char*)zspace) - ((char*)ehdrC);
	 /*         
         fprintf(stderr, "BSS section at 0x%x, size %d\n", 
                         zspace, shdr[i].sh_size);
	 */
      }

      /* fill in the section info */
      if (kind != SECTIONKIND_OTHER && shdr[i].sh_size > 0) {
         addProddableBlock(oc, ehdrC + shdr[i].sh_offset, shdr[i].sh_size);
         addSection(oc, kind, ehdrC + shdr[i].sh_offset, 
                        ehdrC + shdr[i].sh_offset + shdr[i].sh_size - 1);
      }

      if (shdr[i].sh_type != SHT_SYMTAB) continue;

      /* copy stuff into this module's object symbol table */
      stab = (Elf32_Sym*) (ehdrC + shdr[i].sh_offset);
      nent = shdr[i].sh_size / sizeof(Elf32_Sym);

      oc->n_symbols = nent;
      oc->symbols = stgMallocBytes(oc->n_symbols * sizeof(char*), 
                                   "ocGetNames_ELF(oc->symbols)");

      for (j = 0; j < nent; j++) {

         char  isLocal = FALSE; /* avoids uninit-var warning */
         char* ad      = NULL;
         char* nm      = strtab + stab[j].st_name;
         int   secno   = stab[j].st_shndx;

	 /* Figure out if we want to add it; if so, set ad to its
            address.  Otherwise leave ad == NULL. */

         if (secno == SHN_COMMON) {
            isLocal = FALSE;
            ad = stgCallocBytes(1, stab[j].st_size, "ocGetNames_ELF(COMMON)");
	    /*
            fprintf(stderr, "COMMON symbol, size %d name %s\n", 
                            stab[j].st_size, nm);
	    */
	    /* Pointless to do addProddableBlock() for this area,
               since the linker should never poke around in it. */
	 }
         else
         if ( ( ELF32_ST_BIND(stab[j].st_info)==STB_GLOBAL
                || ELF32_ST_BIND(stab[j].st_info)==STB_LOCAL
              )
              /* and not an undefined symbol */
              && stab[j].st_shndx != SHN_UNDEF
	      /* and not in a "special section" */
              && stab[j].st_shndx < SHN_LORESERVE
              &&
	      /* and it's a not a section or string table or anything silly */
              ( ELF32_ST_TYPE(stab[j].st_info)==STT_FUNC ||
                ELF32_ST_TYPE(stab[j].st_info)==STT_OBJECT ||
                ELF32_ST_TYPE(stab[j].st_info)==STT_NOTYPE 
              )
            ) {
	    /* Section 0 is the undefined section, hence > and not >=. */
            ASSERT(secno > 0 && secno < ehdr->e_shnum);
	    /*            
            if (shdr[secno].sh_type == SHT_NOBITS) {
               fprintf(stderr, "   BSS symbol, size %d off %d name %s\n", 
                               stab[j].st_size, stab[j].st_value, nm);
            }
            */
            ad = ehdrC + shdr[ secno ].sh_offset + stab[j].st_value;
            if (ELF32_ST_BIND(stab[j].st_info)==STB_LOCAL) {
               isLocal = TRUE;
            } else {
               IF_DEBUG(linker,belch( "addOTabName(GLOB): %10p  %s %s",
                                      ad, oc->fileName, nm ));
               isLocal = FALSE;
            }
         }

         /* And the decision is ... */

         if (ad != NULL) {
            ASSERT(nm != NULL);
	    oc->symbols[j] = nm;
            /* Acquire! */
            if (isLocal) {
               /* Ignore entirely. */
            } else {
               ghciInsertStrHashTable(oc->fileName, symhash, nm, ad);
            }
         } else {
            /* Skip. */
            IF_DEBUG(linker,belch( "skipping `%s'", 
                                   strtab + stab[j].st_name ));
            /*
            fprintf(stderr, 
                    "skipping   bind = %d,  type = %d,  shndx = %d   `%s'\n",
                    (int)ELF32_ST_BIND(stab[j].st_info), 
                    (int)ELF32_ST_TYPE(stab[j].st_info), 
                    (int)stab[j].st_shndx,
                    strtab + stab[j].st_name
                   );
            */
            oc->symbols[j] = NULL;
         }

      }
   }

   return 1;
}


/* Do ELF relocations which lack an explicit addend.  All x86-linux
   relocations appear to be of this form. */
static int
do_Elf32_Rel_relocations ( ObjectCode* oc, char* ehdrC,
                           Elf32_Shdr* shdr, int shnum, 
                           Elf32_Sym*  stab, char* strtab )
{
   int j;
   char *symbol;
   Elf32_Word* targ;
   Elf32_Rel*  rtab = (Elf32_Rel*) (ehdrC + shdr[shnum].sh_offset);
   int         nent = shdr[shnum].sh_size / sizeof(Elf32_Rel);
   int target_shndx = shdr[shnum].sh_info;
   int symtab_shndx = shdr[shnum].sh_link;
   stab  = (Elf32_Sym*) (ehdrC + shdr[ symtab_shndx ].sh_offset);
   targ  = (Elf32_Word*)(ehdrC + shdr[ target_shndx ].sh_offset);
   IF_DEBUG(linker,belch( "relocations for section %d using symtab %d",
                          target_shndx, symtab_shndx ));
   for (j = 0; j < nent; j++) {
      Elf32_Addr offset = rtab[j].r_offset;
      Elf32_Word info   = rtab[j].r_info;

      Elf32_Addr  P  = ((Elf32_Addr)targ) + offset;
      Elf32_Word* pP = (Elf32_Word*)P;
      Elf32_Addr  A  = *pP;
      Elf32_Addr  S;

      IF_DEBUG(linker,belch( "Rel entry %3d is raw(%6p %6p)", 
                             j, (void*)offset, (void*)info ));
      if (!info) {
         IF_DEBUG(linker,belch( " ZERO" ));
         S = 0;
      } else {
         Elf32_Sym sym = stab[ELF32_R_SYM(info)];
	 /* First see if it is a local symbol. */
         if (ELF32_ST_BIND(sym.st_info) == STB_LOCAL) {
            /* Yes, so we can get the address directly from the ELF symbol
               table. */
            symbol = sym.st_name==0 ? "(noname)" : strtab+sym.st_name;
            S = (Elf32_Addr)
                (ehdrC + shdr[ sym.st_shndx ].sh_offset
                       + stab[ELF32_R_SYM(info)].st_value);

	 } else {
            /* No, so look up the name in our global table. */
            symbol = strtab + sym.st_name;
            (void*)S = lookupSymbol( symbol );
	 }
         if (!S) {
            belch("%s: unknown symbol `%s'", oc->fileName, symbol);
	    return 0;
         }
         IF_DEBUG(linker,belch( "`%s' resolves to %p", symbol, (void*)S ));
      }
      IF_DEBUG(linker,belch( "Reloc: P = %p   S = %p   A = %p",
			     (void*)P, (void*)S, (void*)A )); 
      checkProddableBlock ( oc, pP );
      switch (ELF32_R_TYPE(info)) {
#        ifdef i386_TARGET_ARCH
         case R_386_32:   *pP = S + A;     break;
         case R_386_PC32: *pP = S + A - P; break;
#        endif
         default: 
            belch("%s: unhandled ELF relocation(Rel) type %d\n",
		  oc->fileName, ELF32_R_TYPE(info));
            return 0;
      }

   }
   return 1;
}


/* Do ELF relocations for which explicit addends are supplied.
   sparc-solaris relocations appear to be of this form. */
static int
do_Elf32_Rela_relocations ( ObjectCode* oc, char* ehdrC,
                            Elf32_Shdr* shdr, int shnum, 
                            Elf32_Sym*  stab, char* strtab )
{
   int j;
   char *symbol;
   Elf32_Word* targ;
   Elf32_Rela* rtab = (Elf32_Rela*) (ehdrC + shdr[shnum].sh_offset);
   int         nent = shdr[shnum].sh_size / sizeof(Elf32_Rela);
   int target_shndx = shdr[shnum].sh_info;
   int symtab_shndx = shdr[shnum].sh_link;
   stab  = (Elf32_Sym*) (ehdrC + shdr[ symtab_shndx ].sh_offset);
   targ  = (Elf32_Word*)(ehdrC + shdr[ target_shndx ].sh_offset);
   IF_DEBUG(linker,belch( "relocations for section %d using symtab %d",
                          target_shndx, symtab_shndx ));
   for (j = 0; j < nent; j++) {
      Elf32_Addr  offset = rtab[j].r_offset;
      Elf32_Word  info   = rtab[j].r_info;
      Elf32_Sword addend = rtab[j].r_addend;
      Elf32_Addr  P  = ((Elf32_Addr)targ) + offset;
      Elf32_Addr  A  = addend; /* Do not delete this; it is used on sparc. */
      Elf32_Addr  S;
#     if defined(sparc_TARGET_ARCH)
      /* This #ifdef only serves to avoid unused-var warnings. */
      Elf32_Word* pP = (Elf32_Word*)P;
      Elf32_Word  w1, w2;
#     endif

      IF_DEBUG(linker,belch( "Rel entry %3d is raw(%6p %6p %6p)   ", 
                             j, (void*)offset, (void*)info, 
                                (void*)addend ));
      if (!info) {
         IF_DEBUG(linker,belch( " ZERO" ));
         S = 0;
      } else {
         Elf32_Sym sym = stab[ELF32_R_SYM(info)];
	 /* First see if it is a local symbol. */
         if (ELF32_ST_BIND(sym.st_info) == STB_LOCAL) {
            /* Yes, so we can get the address directly from the ELF symbol
               table. */
            symbol = sym.st_name==0 ? "(noname)" : strtab+sym.st_name;
            S = (Elf32_Addr)
                (ehdrC + shdr[ sym.st_shndx ].sh_offset
                       + stab[ELF32_R_SYM(info)].st_value);

	 } else {
            /* No, so look up the name in our global table. */
            symbol = strtab + sym.st_name;
            (void*)S = lookupSymbol( symbol );
	 }
         if (!S) {
	   belch("%s: unknown symbol `%s'", oc->fileName, symbol);
	   return 0;
	   /* 
	   S = 0x11223344;
	   fprintf ( stderr, "S %p A %p S+A %p S+A-P %p\n",S,A,S+A,S+A-P);
	   */
         }
         IF_DEBUG(linker,belch( "`%s' resolves to %p", symbol, (void*)S ));
      }
      IF_DEBUG(linker,fprintf ( stderr, "Reloc: P = %p   S = %p   A = %p\n",
                                        (void*)P, (void*)S, (void*)A )); 
      checkProddableBlock ( oc, (void*)P );
      switch (ELF32_R_TYPE(info)) {
#        if defined(sparc_TARGET_ARCH)
         case R_SPARC_WDISP30: 
            w1 = *pP & 0xC0000000;
            w2 = (Elf32_Word)((S + A - P) >> 2);
            ASSERT((w2 & 0xC0000000) == 0);
            w1 |= w2;
            *pP = w1;
            break;
         case R_SPARC_HI22:
            w1 = *pP & 0xFFC00000;
            w2 = (Elf32_Word)((S + A) >> 10);
            ASSERT((w2 & 0xFFC00000) == 0);
            w1 |= w2;
            *pP = w1;
            break;
         case R_SPARC_LO10:
            w1 = *pP & ~0x3FF;
            w2 = (Elf32_Word)((S + A) & 0x3FF);
            ASSERT((w2 & ~0x3FF) == 0);
            w1 |= w2;
            *pP = w1;
            break;
         /* According to the Sun documentation:
            R_SPARC_UA32 
            This relocation type resembles R_SPARC_32, except it refers to an
            unaligned word. That is, the word to be relocated must be treated
            as four separate bytes with arbitrary alignment, not as a word
            aligned according to the architecture requirements.

            (JRS: which means that freeloading on the R_SPARC_32 case
            is probably wrong, but hey ...)  
         */
         case R_SPARC_UA32:
         case R_SPARC_32:
            w2 = (Elf32_Word)(S + A);
            *pP = w2;
            break;
#        endif
         default: 
            belch("%s: unhandled ELF relocation(RelA) type %d\n",
		  oc->fileName, ELF32_R_TYPE(info));
            return 0;
      }

   }
   return 1;
}


static int
ocResolve_ELF ( ObjectCode* oc )
{
   char *strtab;
   int   shnum, ok;
   Elf32_Sym*  stab = NULL;
   char*       ehdrC = (char*)(oc->image);
   Elf32_Ehdr* ehdr = (Elf32_Ehdr*) ehdrC;
   Elf32_Shdr* shdr = (Elf32_Shdr*) (ehdrC + ehdr->e_shoff);
   char* sh_strtab  = ehdrC + shdr[ehdr->e_shstrndx].sh_offset;

   /* first find "the" symbol table */
   stab = (Elf32_Sym*) findElfSection ( ehdrC, SHT_SYMTAB );

   /* also go find the string table */
   strtab = findElfSection ( ehdrC, SHT_STRTAB );

   if (stab == NULL || strtab == NULL) {
      belch("%s: can't find string or symbol table", oc->fileName);
      return 0; 
   }

   /* Process the relocation sections. */
   for (shnum = 0; shnum < ehdr->e_shnum; shnum++) {

      /* Skip sections called ".rel.stab".  These appear to contain
         relocation entries that, when done, make the stabs debugging
         info point at the right places.  We ain't interested in all
         dat jazz, mun. */
      if (0 == strncmp(".rel.stab", sh_strtab + shdr[shnum].sh_name, 9))
         continue;

      if (shdr[shnum].sh_type == SHT_REL ) {
         ok = do_Elf32_Rel_relocations ( oc, ehdrC, shdr, 
                                         shnum, stab, strtab );
         if (!ok) return ok;
      }
      else
      if (shdr[shnum].sh_type == SHT_RELA) {
         ok = do_Elf32_Rela_relocations ( oc, ehdrC, shdr, 
                                          shnum, stab, strtab );
         if (!ok) return ok;
      }

   }

   /* Free the local symbol table; we won't need it again. */
   freeHashTable(oc->lochash, NULL);
   oc->lochash = NULL;

   return 1;
}


#endif /* ELF */

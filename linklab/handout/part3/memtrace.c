//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//

#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>

//
// function pointers to stdlib's memory management functions
//
static void *(*mallocp)(size_t size) = NULL;
static void (*freep)(void *ptr) = NULL;
static void *(*callocp)(size_t nmemb, size_t size);
static void *(*reallocp)(void *ptr, size_t size);
static void realloc_free(void* ptr);

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;


//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

  // ...
}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
  // ...
  int flag=0;
  LOG_STATISTICS(n_allocb,n_allocb/(n_malloc+n_calloc+n_realloc), n_freeb);

  list = list->next;
  while(list!=NULL){
    if(list->cnt>0){
        if(flag==0){
            LOG_NONFREED_START();
            flag=1;
        }
        LOG_BLOCK(list->ptr, list->size, list->cnt);
    }
    list=list->next;
  }

  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
}

// ...
void *malloc(size_t size){
  char *error;
  void *ptr;

  if (!mallocp){
    mallocp=dlsym(RTLD_NEXT, "malloc");
    if ((error=dlerror())!=NULL){
      fputs(error, stderr);
      exit(1);
    }
  }

  ptr=mallocp(size);
  LOG_MALLOC(size, ptr);
  alloc(list, ptr, size);
  n_allocb+=size;
  n_malloc++;
  return ptr;
}

void *calloc(size_t nmemb, size_t size){
  char *error;
  void *ptr;

  if (!callocp){
    callocp=dlsym(RTLD_NEXT, "calloc");
    if ((error=dlerror())!=NULL){
      fputs(error, stderr);
      exit(1);
    }
  }

  ptr=callocp(nmemb, size);
  LOG_CALLOC(nmemb, size, ptr);
  alloc(list, ptr, nmemb*size);
  n_allocb+=nmemb*size;
  n_calloc++;
  return ptr;
}

void *realloc(void* ptr, size_t size){
  char *error;
  void *newptr;
  item* freed_ptr;

  if (!reallocp){
    reallocp=dlsym(RTLD_NEXT, "realloc");
    if ((error=dlerror())!=NULL){
      fputs(error, stderr);
      exit(1);
    }
  }
  if (!mallocp){
    mallocp=dlsym(RTLD_NEXT, "malloc");
    if ((error=dlerror())!=NULL){
      fputs(error, stderr);
      exit(1);
    }
  }


  if(find(list, ptr)==NULL){
    newptr=mallocp(size);
    LOG_REALLOC(ptr, size, newptr);
    LOG_ILL_FREE();
  }
  else if(find(list, ptr)->cnt>0){
    freed_ptr = dealloc(list, ptr);
    n_freeb+=freed_ptr->size;
    newptr=reallocp(ptr, size); //only call real realloc when ptr is legal.
    LOG_REALLOC(ptr, size, newptr);
  }
  else{
    newptr=mallocp(size);
    LOG_REALLOC(ptr, size, newptr);
    LOG_DOUBLE_FREE();
  }

  alloc(list, newptr, size); 
  n_allocb+=size;
  n_realloc++;
  return newptr;
}

void free(void* ptr){
  char *error;
  item* freed_ptr;

  if (!freep){
    freep=dlsym(RTLD_NEXT, "free");
    if ((error=dlerror())!=NULL){
      fputs(error, stderr);
      exit(1);
    }
  }

  if(find(list, ptr)==NULL){
    LOG_FREE(ptr);
    LOG_ILL_FREE();
  }
  else if(find(list, ptr)->cnt>0){
    freed_ptr = dealloc(list, ptr);
    n_freeb+=freed_ptr->size;
    freep(ptr);
    LOG_FREE(ptr);
  }
  else{
    LOG_FREE(ptr);
    LOG_DOUBLE_FREE();
  }
}







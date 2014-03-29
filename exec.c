#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

#define MAX_PATH_ENTRIES 10
#define INPUT_BUF 128 // This is the same value from console.c
struct {
  char entries[MAX_PATH_ENTRIES][INPUT_BUF];
  uint size; // Initialized by compiler to default value of 0
} kernel_path;

int
sys_add_path(void)
{
  char *path;
  int l;

  // Initialize function argument
  if(argstr(0, &path) < 0)
    return -1;

  // Check to make sure that there is still room in PATH
  if(kernel_path.size == MAX_PATH_ENTRIES)
    return -1;

  safestrcpy(kernel_path.entries[kernel_path.size], path, INPUT_BUF);

  // Append trailing '/' character if necessary
  l = strlen(path);
  if(path[l-1] != '/' && l < INPUT_BUF - 1) {
    kernel_path.entries[kernel_path.size][l] = '/';
    kernel_path.entries[kernel_path.size][l+1] = '\0';
  }

  kernel_path.size++;

  return 0;
}

int
exec(char *path, char **argv)
{
  char full_path[INPUT_BUF];
  char *s, *last;
  int i, l, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;

  if((ip = namei(path)) == 0) {
    // Executable not found in current directory. Try searching for it in
    // kernel_path (PATH)

    for(i=0; i<kernel_path.size; ++i) {
      // We need to make a string that is the concatenation of both:
      // kernel_path.entries[i] + path

      l = strlen(kernel_path.entries[i]);
      safestrcpy(full_path, kernel_path.entries[i], INPUT_BUF);
      safestrcpy(full_path + l, path, INPUT_BUF - l);

      ip = namei(full_path);

      if (ip != 0) {
        path = full_path;
        break;
      }
    }
    if(i == kernel_path.size) {
      // Executable was not found anywhere
      return -1;
    }
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm(kalloc)) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(proc->name, last, sizeof(proc->name));

  // Commit to the user image.
  oldpgdir = proc->pgdir;
  proc->pgdir = pgdir;
  proc->sz = sz;
  proc->tf->eip = elf.entry;  // main
  proc->tf->esp = sp;
  switchuvm(proc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip)
    iunlockput(ip);
  return -1;
}

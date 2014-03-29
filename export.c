#include "types.h"
#include "user.h"

#define PATH_SEP ':'

void
add_separated_paths(char *pathstr)
{
  char *start = pathstr;
  char *cur;

  while(*start == PATH_SEP)
    start++;

  if(*start == '\0')
    return;

  cur = start;

  while(1) {
    // Invariants here:
    //   *start != '\0'
    //   *start != PATH_SEP

    if(*cur == '\0') {
      add_path(start);
      return;
    }

    if(*cur == PATH_SEP) {
      *cur = '\0';
      add_path(start);
      start = cur + 1;
      while(*start == PATH_SEP)
        start++;
      if(*start == '\0')
        return;
      cur = start;
    }

    cur++;
  }
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf(2, "usage: export path1[:path2[:...]]\n");
    exit();
  }

  add_separated_paths(argv[1]);
  exit();
}

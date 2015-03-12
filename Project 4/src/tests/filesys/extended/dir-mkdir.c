/* Tests mkdir(). */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  
  CHECK (mkdir ("a"), "mkdir \"a\"");
  CHECK (create ("a/b", 512), "create \"a/b\"");
  CHECK (chdir ("a"), "chdir \"a\"");
  CHECK (open ("b") > 1, "open \"b\"");
  

  /*
  CHECK (mkdir("/0"), "mkdir \"/0\"");
  CHECK (mkdir("/0/1"), "mkdir \"/0/1\"");
  CHECK (chdir("/0"), "chdir \"/0\"");
  CHECK (open("1"), "open \"1\"");
  */
}



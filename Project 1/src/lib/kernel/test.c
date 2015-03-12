#include <stdio.h>
#include "list.h"

struct test
{
  struct list_elem elem;
  int priority;
};


void main()
{

  int i;
  struct list test_list;
  list_init(&test_list);

  struct test test1;
  struct test test2;
  struct test test3;

  test1.priority = 1;
  test2.priority = 2;
  test3.priority = 3;
  
  list_insert(&(test_list.head), &test3.elem);
  list_insert(&(test_list.head), &test1.elem);
  list_insert(&(test_list.head), &test2.elem);

  
  printf("list size is %d\n", list_size(&test_list));
}

// real 9.05
// user 8.31

#include<stdio.h>
#include "dummy_main.h"

int fib(int n) {
  if(n<2) return n;
  else return fib(n-1)+fib(n-2);
}

int main() {
	int val = fib(46);
  printf("value is %d\n", val);
	return 0;
}
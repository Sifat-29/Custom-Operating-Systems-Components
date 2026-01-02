// real 2.28
// user 2.08

#include<stdio.h>
#include "dummy_main.h" 

int fib(int n) {
  if(n<2) return n;
  else return fib(n-1)+fib(n-2);
}

int main() {
	int val = fib(43);
  printf("value is %d\n", val);
	return 0;
}

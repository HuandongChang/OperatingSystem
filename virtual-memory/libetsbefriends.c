#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
// Some quotes from https://www.afi.com/afis-100-years-100-movie-quotes/

void seg_fault_msg(int signal, siginfo_t* info, void* ctx) {
  //using time to get random number
  time_t t;
  srand((unsigned)time(&t));
  //list of messages
  char* msgs[8] = {"Life ain't all sunshine and segmentation faults.",
                   "I've got a feeling we're not in allocated memory anymore",
                   "What we've got here is a communication failure",
                   "Love means never having segmentation faults <3",
                   "They call me Mr. Seg Fault",
                   "Is it safe?",
                   "You can't handle the truth!",
                   "Seg faults? We ain't got no seg faults! We don't need no seg faults! I don't "
                   "have to show you "
                   "any stinking seg faults!"};
  //printing random message
  printf("%s\n", msgs[rand() % 8]);
  exit(1);
}

// TODO: Implement your signal handling code here!
__attribute__((constructor)) void init() {
  //printf("This code runs at program startup\n");
  //getting seg fault struct and changing to send out our message
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sa.sa_sigaction = seg_fault_msg;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGSEGV, &sa, NULL) != 0) {
    perror("seg fault failed");
    exit(2);
  }
}

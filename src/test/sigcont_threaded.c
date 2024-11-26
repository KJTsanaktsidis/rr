/* -*- Mode: C; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#include "util.h"

static void *child_process_extra_thread(__attribute__((unused)) void *extra_thread) {
  int r;

  atomic_printf("Child thread is %d\n", gettid());

  // Slap in a sched_yield or two here so that the parent process is going to be
  // blocked in pthread_join.
  sched_yield();
  sched_yield();

  // Now, stop ourselves. We'll be unstopped by the parent process.
  atomic_printf("Child thread stopping\n");
  r = kill(gettid(), SIGSTOP);
  atomic_printf("Child thread resumed\n");
  test_assert(r == 0);

  // Now allow self to exit, and the thread-group-leader can continue.
  return NULL;
}

static void child_process(void) {
  pthread_t extra_thread;
  int r;
  // Spawn an additional thread
  r = pthread_create(&extra_thread, NULL, child_process_extra_thread, NULL);
  test_assert(r == 0);

  // Wait for the child thread we made. It will send SIGSTOP to the process.
  r = pthread_join(extra_thread, NULL);
  atomic_printf("Child thread joined\n");
  test_assert(r == 0);
}

static void parent_process(pid_t pid) {
  int wait_status, r;
  pid_t wpid;

  // Wait for the child process to have sent itself SIGSTOP
  wpid = waitpid(pid, &wait_status, WUNTRACED);
  atomic_printf("parent process first waitpid result %d\n", wpid);
  test_assert(wpid == pid);
  test_assert(WIFSTOPPED(wait_status));
  test_assert(WSTOPSIG(wait_status) == SIGSTOP);

  // Let it continue
  atomic_printf("parent process continuing\n");
  r = kill(pid, SIGCONT);
  test_assert(r == 0);

  // Now the child process should actually exit
  wpid = waitpid(pid, &wait_status, 0);
  atomic_printf("parent process second waitpid result %d\n", wpid);
  test_assert(wpid == pid);
  test_assert(WIFEXITED(wait_status));
}

static void sighandler(__attribute__((unused)) int sig,
                       siginfo_t *si,
                       __attribute__((unused)) void *ctx) {
  atomic_printf("process %d got SIGCHLD for pid %d\n", getpid(), si->si_pid);
}


int main(void) {
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESTART;
  sa.sa_sigaction = sighandler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, NULL);

  atomic_printf("Parent process is %d\n", getpid());
  pid_t pid = fork();
  test_assert(pid != -1);
  if (pid == 0) {
    atomic_printf("Child process is %d\n", getpid());
    child_process();
  } else {
    parent_process(pid);
    atomic_puts("EXIT-SUCCESS");
  }
  return 0;
}

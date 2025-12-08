#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

int main() {
    // 1. Standard C Library wrapper for system call
    // This wrapper eventually calls the kernel
    const char msg1[] = "Hello from the standard wrapper!\n";
    write(1, msg1, sizeof(msg1) - 1);

    // 2. Direct System Call (Linux specific)
    // We manually invoke the syscall number for 'write' (syscall #1 on x86_64)
    // Note: This is architecture dependent!
    const char msg2[] = "Hello from raw syscall!\n";
    syscall(SYS_write, 1, msg2, sizeof(msg2) - 1);

    // 3. Get Process ID syscall
    pid_t pid = getpid();
    
    // Quick and dirty way to print numbers without printf (which uses buffers)
    // We are staying "close to the metal"
    char pid_msg[] = "\nMy PID is: ";
    write(1, pid_msg, sizeof(pid_msg) - 1);
    
    return 0;
}
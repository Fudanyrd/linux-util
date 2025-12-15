import typing
import sys
import subprocess

def get_syscallno(name: str) -> int:
    """
    Get syscall number from `gcc -E`.
    """
    txt = f"#include <syscall.h>\nSYS_{name}\n"
    proc = subprocess.run(['gcc', '-E', '-'], capture_output=True, text=True, input=txt)
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line and not line.startswith('#'):
            return int(line)
    raise ValueError(f'Syscall name {name} not found.')

X86_64_CONFIG: dict = {
    "int_ret": "%eax",
    "long_ret": "%rax",
    "syscall_no": "%eax",
    "syscall_no_inst": "movl",
    "inst": "syscall",

    # move register values.
    "move_inst": (lambda dst, src: f'\tmovq {src}, {dst}'),
    "set_no_inst": (lambda dst, num: f'\tmovl ${num}, {dst}'),

    # calling conventions.
    "arg1": "%rdi",
    "arg2": "%rsi",
    "arg3": "%rdx",
    "arg4": "%rcx",
    "arg5": "%r8",
    "arg6": "%r9",

    # syscall spec.
    "sys_arg1": "%rdi",
    "sys_arg2": "%rsi",
    "sys_arg3": "%rdx",
    "sys_arg4": "%r10", # !
    "sys_arg5": "%r8",
    "sys_arg6": "%r9",

    # save callee-saved registers.
    "on_enter": "",
    # recover callee-saved registers.
    "on_exit": "",
    
    "clobber": "\"rcx\", \"r11\", \"rax\", \"memory\"",
}

def genasm(asmfile: str, decl: dict, syscallno: int, CONFIG: dict = X86_64_CONFIG):
    """
    Example:
    >>> decl = {"rettype": "long", "name": "write", "num_args": 3}
    >>> genasm('write.S', decl, 4)
    """
    name = decl['name']
    with open(asmfile, 'w') as fobj:
        fobj.write('\t.weak ' + name + '\n')
        fobj.write(f'{name}:\n')
        fobj.write(CONFIG["on_enter"])
        for i in range(decl['num_args']):
            if CONFIG["arg" + str(i + 1)] != CONFIG["sys_arg" + str(i + 1)]:
                fobj.write(CONFIG["move_inst"](CONFIG["sys_arg" + str(i + 1)], CONFIG["arg" + str(i + 1)]) + '\n')

        fobj.write(CONFIG["set_no_inst"](CONFIG["syscall_no"], syscallno) + '\n')
        fobj.write(f'    {CONFIG["inst"]}\n')
        fobj.write(CONFIG["on_exit"])
        fobj.write(f'    ret\n')

LINUX_SYSCALLS: list[dict] = [
    {"rettype": "long", "name": "exit_group", "num_args": 1},
    {"rettype": "int", "name": "open", "num_args": 3},
    {"rettype": "int", "name": "close", "num_args": 1},
    {"rettype": "long", "name": "read", "num_args": 3},
    {"rettype": "long", "name": "write", "num_args": 3},
    {"rettype": "long" , "name": "mmap", "num_args": 6}, # void *
    {"rettype": "int", "name": "munmap", "num_args": 2},
    # getdents
    {"rettype": "long", "name": "getdents64", "num_args": 3},
    # openat
    {"rettype": "int", "name": "openat", "num_args": 4},
    # ~~wait, waitpid~~ wait4
    {"rettype": "int", "name": "wait", "num_args": 1},
    {"rettype": "int", "name": "waitpid", "num_args": 3},
    {"rettype": "int", "name": "wait4", "num_args": 4}, # wait4(-1,0,0,0)
    # fork, vfork
    {"rettype": "int", "name": "fork", "num_args": 0},
    {"rettype": "int", "name": "vfork", "num_args": 0},
    # execve
    {"rettype": "int", "name": "execve", "num_args": 3},
]

if __name__ == "__main__":
    objfiles = []
    for decl in LINUX_SYSCALLS:
        try:
            syscallno = get_syscallno(decl['name'])
        except:
            print(decl['name'] + ' is not a valid syscall.')
            continue
        asmfile = f"{decl['name']}.asm"
        objfile = f"{decl['name']}.o"
        genasm(asmfile, decl, syscallno, X86_64_CONFIG) 
        res = subprocess.run(['as', '--64', asmfile, '-o', objfile])
        if res.returncode != 0:
            print(f'Error: Failed to assemble {asmfile}', file=sys.stderr)
            sys.exit(1)
        print(f'Generated {asmfile} (syscall no: {syscallno})', file=sys.stderr)
        objfiles.append(objfile)
    res = subprocess.run(['ar', 'rcs', 'libsyscall.a'] + objfiles)
    if res.returncode != 0:
        print(f'Error: Failed to create library libsyscall.a', file=sys.stderr)
        sys.exit(1)
    _ = subprocess.run(['rm'] + objfiles)

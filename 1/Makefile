GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: libcoro.c solution.c
	gcc $(GCC_FLAGS) -Wl,--no-as-needed -ldl -rdynamic ../utils/heap_help/heap_help.c libcoro.c solution.c

clean:
	rm a.out
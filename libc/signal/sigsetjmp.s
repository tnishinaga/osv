/* Copyright 2011-2012 Nicholas J. Kain, licensed under standard MIT license */
.global __sigsetjmp
.type __sigsetjmp,@function
__sigsetjmp:
	andl %esi,%esi
	movq %rsi,64(%rdi)
	jz 1f
	pushq %rdi
	leaq 72(%rdi),%rsi
	xorl %edx,%edx
	movl $2,%edi
	call sigprocmask
	popq %rdi
1:	jmp setjmp

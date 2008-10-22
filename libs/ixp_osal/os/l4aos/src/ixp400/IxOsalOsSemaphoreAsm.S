#include <l4/arch/asm.h>
#include <l4/arch/vregs_asm.h>

#ifdef __arm
	AREA	ixOsalFastMutexLock, CODE
	IMPORT	L4_ThreadSwitch
#endif
	
#ifdef L4_BIG_ENDIAN
#define TCR_PREEMPT_FLAGS (3 + __L4_TCR_PREEMPT_FLAGS*4)
#else // L4_LITLE_ENDIAN
#define TCR_PREEMPT_FLAGS (    __L4_TCR_PREEMPT_FLAGS*4)
#endif

	BEGIN_PROC(ixOsalFastMutexLock)
	/* r0 contains the lock address */
	stmdb	sp!, {r4, r5, lr} /* r4 and r5 are local variables, 
				    lr needed when we do the syscall */

	/* r4 -- saved lock address */
	ldr	r4, [r0]
	/* Here we load the utcb address */
	mov	r3, #0xff000000
	/* r5 -- saved UTCB address */
	ldr	r5, [r3, #0xff0]

	/* From here r4 and r5 are safe, the thread_switch
		syscall will not trash these registers */
	
	/* r0 is tmp */
	/* First we set the point at which we want to
	   be restarted in case of preemption */
	adr	r0, preempt_handler
	str	r0, [r5, #__L4_TCR_PREEMPT_CALLBACK_IP*4]
	/* And then we enable the preemption callback to
	   occur */
	mov	r0, #32
	strb	r0, [r5, #TCR_PREEMPT_FLAGS]
LABEL(preempt_handler)            /* If preempt we restart here */
	/* r0 is lock holder */
	ldr	r0, [r4]
	/* r1 is me (real tid is in the user-defined handle)*/
	ldr	r1, [r5, #__L4_TCR_USER_DEFINED_HANDLE*4]
	/* test if lock holder is == 0 */
	cmp	r0, #0
	beq	grab_lock
	/* or if lock holder is me */
	cmp	r0, r1
	beq	exit_path   /* We already have the lock so we jump forward to
                               the part where we turn off preemption and return */
	/* we couldn't get the lock so we fall through to here  */
	/* r0 holds lock holder, will be argument to the system call */
	/* r1 is temp */

	/* Load syscall ptr */
	ldr	ip, =L4_ThreadSwitch

	mov	r1, #1
	str	r1, [r4, #4] /* Let current lock holder know there is contention
                                so that it knows to yield at the end of its timeslice */

	/* Load branch address */
	/* ldr	ip, [ip] */

	stmdb	sp!, {r4-r11}
	mov	lr, pc
	/* do the system call */
	mov	pc, ip

	ldmia	sp!, {r4-r11}
	/* After syscall return to preempt_handler */
	b	preempt_handler
LABEL(grab_lock)
	/* The lock is free -- we try to grab it before being preempted */
	/* r0 is tmp */
	mov	r0, #0
	str	r0, [r4, #4] /* If we get this far, then noone of a higher priority than
		                us wants the lock, so we can unset the yield needed flag */
	/* Now we store ourself as the lock handler, this is transaction complete, although
	   we still might be preempted right here, in which case we validaly have the lock
	   and the preempt handler will go through sucessfully */
	str	r1, [r4]
	strb	r0, [r5, #TCR_PREEMPT_FLAGS]
	ldmia	sp!, {r4, r5, pc} /* RETURN POINT */
LABEL(exit_path)
	/* Exit path that occurs if we were preempted, before returning --
	   same as above, however we need to zero, r0 first */
	mov	r0, #0
	strb	r0, [r5, #TCR_PREEMPT_FLAGS]
	ldmia	sp!, {r4, r5, pc} /* RETURN POINT */
	END_PROC(ixOsalFastMutexLock)







	BEGIN_PROC(ixOsalFastMutexTryLock)
	/* r0 contains the lock address */
	stmdb	sp!, {r4, r5} /* r4 and r5 are local variables */

	/* r4 -- saved lock address */
	ldr	r4, [r0]
	/* Here we load the utcb address */
	mov	r3, #0xff000000
	/* r5 -- saved UTCB address */
	ldr	r5, [r3, #0xff0]

	/* From here r4 and r5 are safe, the thread_switch
		syscall will not trash these registers */
	
	/* r0 is tmp */
	/* First we set the point at which we want to
	   be restarted in case of preemption */
	adr	r0, try_preempt_handler
	str	r0, [r5, #__L4_TCR_PREEMPT_CALLBACK_IP*4]
	/* And then we enable the preemption callback to
	   occur */
	mov	r0, #32
	strb	r0, [r5, #TCR_PREEMPT_FLAGS]
LABEL(try_preempt_handler)            /* If preempt we restart here */
	/* r0 is lock holder */
	ldr	r0, [r4]
	/* r1 is me */
	ldr	r1, [r5, #__L4_TCR_USER_DEFINED_HANDLE*4]
	/* test if lock holder is == 0 */
	cmp	r0, #0
	beq	try_grab_lock

	/* or if lock holder is me */
	subs	r0, r0, r1

	mov	r1, #0
	strb	r1, [r5, #TCR_PREEMPT_FLAGS]

	movne	r0, #1	/* Somebody else has the lock */

	/* Exit path that occurs if we were preempted, before returning --
	   same as try_grab_lock, however we need to zero, r0 first */
	ldmia	sp!, {r4, r5} /* RETURN POINT */
	mov	pc, lr

LABEL(try_grab_lock)
	/* The lock is free -- we try to grab it before being preempted */
	/* r0 is tmp */
	mov	r0, #0
	str	r0, [r4, #4] /* If we get this far, then noone of a higher priority than
		                us wants the lock, so we can unset the yield needed flag */
	/* Now we store ourself as the lock handler, this is transaction complete, although
	   we still might be preempted right here, in which case we validaly have the lock
	   and the preempt handler will go through sucessfully */
	str	r1, [r4]
	strb	r0, [r5, #TCR_PREEMPT_FLAGS]
	ldmia	sp!, {r4, r5} /* RETURN POINT */
	mov	pc, lr

	END_PROC(ixOsalFastMutexTryLock)
	END

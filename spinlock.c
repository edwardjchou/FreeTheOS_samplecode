#include "spinlock.h"
#include "lib.h"
/*
* void spin_lock(spinlock_t * lock)
*   Inputs: spinlock_t * lock
*   Outputs: none
*	Spins until lock is not zero (unlocked), then grabs and sets lock to 1
*/
void spin_lock(spinlock_t * lock){
	//set ebx as the lock register, "b"
	//cc is condition code, eax is clobbered
  asm volatile("                    \n\
                .spin_lock_spin:    \n\
                mov   $1,%%eax      \n\
                xchg  %%eax,(%%ebx) \n\
                test  %%eax,%%eax   \n\
                jnz   .spin_lock_spin\n\
                "
                :/**/
                :"b"(lock)
                :"cc","eax","memory"         );
}
/*
* void spin_unlock(spinlock_t * lock)
*   Inputs: spinlock_t * lock
*   Outputs: none
*	Releases the lock; sets lock back to 0
*/
void spin_unlock(spinlock_t * lock){
  asm volatile("                    \n\
                mov   $0,%%eax      \n\
                xchg  %%eax,(%%ebx) \n\
                "
                :/**/
                :"b"(lock)
                :"cc","eax","memory"         );
}
/*
void block_interrupts(uint32_t * flags){
  asm volatile("                    \n\
                pushfl              \n\
                popl  (%%eax)       \n\
                cli                 \n\
                "
                :
                :"a"(flags)
                :"cc"               );
}
void unblock_interrupts(uint32_t * flags){
  asm volatile("                    \n\
                pushl (%%eax)       \n\
                popfl               \n\
                "
                :
                :"a"(flags)
                :"cc"               );
}*/

/*
* void spin_lock_irq(spinlock_t * lock)
*   Inputs: spinlock_t * lock
*   Outputs: none
*	Spin lock with cli() (clear interrupts) setting IF to 0 to disable interrupts
*/
void spin_lock_irq(spinlock_t * lock){
  sti();
  spin_lock(lock);
  cli();
}
/*
* void spin_unlock_irq(spinlock_t * lock)
*   Inputs: spinlock_t * lock
*   Outputs: none
*	Spin unlock with sti() (set interrupts) setting IF to 1 to enable interrupts
*/
void spin_unlock_irq(spinlock_t * lock){
  spin_unlock(lock);
  sti();
}
/*
* void spin_lock_irqsave(spinlock_t * lock, uint32_t * flags)
*   Inputs: spinlock_t * lock, uint32_t * flags
*   Outputs: none
*	Save the interrupt flags while cli() and spinlocking
*/
void spin_lock_irqsave(spinlock_t * lock, uint32_t * flags){
  //block_interrupts(flags);
  sti();
  spin_lock(lock);
  cli_and_save(*flags); //pops flags then cli
}
/*
* void spin_unlock_irqrestore(spinlock_t * lock, uint32_t * flags)
*   Inputs: spinlock_t * lock, uint32_t * flags
*   Outputs: none
*	Restore saved flags while sti() and spin unlocking
*/
void spin_unlock_irqrestore(spinlock_t * lock, uint32_t * flags){
  spin_unlock(lock);
  sti();
  restore_flags(*flags);
  //important, sti() first, then restore flags
//  sti();
  //unblock_interrupts(flags);
}

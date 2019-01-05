# RCUMutex

## Summary

RCUMutex was created because of the need to have a shared/read lock for a very small critical section. 
The mutex uses RCU in this implamintaion to allow only one active thread to update a single atomic value at a time, minimizing the time it takes to syncronize cache lines between cores. 

RCUMutex works by using by using RCU syncronize to block for exclusive lock while a reader (shared lock) is still active.
To allow for exclusize locks to block shared locking the most significate bit of the counter acts as an exclusive flag. 
When the flag is set accquiring a shared and exclusive lock is block until the flag is cleared. 


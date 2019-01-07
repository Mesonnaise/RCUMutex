# RCUMutex

## Summary

RCUMutex was created because of the need to have a shared/read lock for a very small critical section. 
The mutex uses RCU in this implamintaion to allow only one active thread to update a single atomic value at a time, minimizing the time it takes to syncronize cache lines between cores. 

RCUMutex works by using by using RCU syncronize to block for exclusive lock while a reader (shared lock) is still active.
To allow for exclusize locks to block shared locking the most significate bit of the counter acts as an exclusive flag. 
When the flag is set accquiring a shared and exclusive lock is block until the flag is cleared. 


## Usage

Mutexes are not shared between threads for a given resource, instead are maintained by the thread needing access to a particular resource. A mutex can be aquired through the `GetMutex` method of the `RCUManager`.


````C++

#include<vector>
#include<chrono>
#include<iostream>
#include<functional>

#include<mutex>
#include<shared_mutex>
#include<thread>

#include"RCUMutex.h"

struct SharedResource {
  RCUManager mutexManager;
  int        resourceUnderContention;

  SharedResource(size_t threadCount):mutexManager{threadCount}{}
}

void read(SharedResource &res,unsigned int threadId){
  RCUManager::Mutex mutex=res.mutexManager.GetMutex();

  for(int i=0;i<10;i++){
  
    {
      std::shared_lock<RCUManager::Mutex> lock(mutex);

      std::cout<<"thread "<<threadId<<" resource value is "
               <<res.resourceUnderContention<<" \n";
    }
	
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void write(SharedResource &res){
  RCUManager::Mutex mutex=res.mutexManager.GetMutex();

  for(int i=0;i<10;i++){
  
    {
      std::lock_guard<RCUManager::Mutex> lock(mutex);
	  
      res.resourceUnderContention++;
    }
	
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

int main(){  
  const size_t             totalThreads=8;

  SharedResource           res(totalThreads);

  std::vector<std::thread> sharedThreads;

  for(unsigned int i=0;i<totalThreads-1;i++)
    sharedThreads.emplace_back(read,std::ref(res),i);

  std::thread w1(write,std::ref(res));

  for(auto &t:sharedThreads)
    t.join();

  w1.join();

  return 0;
}

````

#pragma once
#include<cinttypes>
#include<atomic>
#include<vector>

//Implementation is based off paper 
//Grace Sharing Userspace RCU
//by Pedro Ramalhete and Andereia Correia
//DOI: 1.4230/LIPIcs.CVIT.2016.23

class RCUManager{
private:
  //Each of the counters needs to sit on a different cache line
  //to avoid having multiable counters from invaliding 
  //the same cache line when updated.
  //zen=64bytes,intel=128bytes
  static constexpr int      ALIGNMENT =64;
  static constexpr uint64_t UNASSIGNED=0xFFFFFFFFFFFFFFFFULL;
  static constexpr uint64_t IDLE      =0xFFFFFFFFFFFFFFFEULL;
  static constexpr uint64_t INCLOCK   =0x8000000000000001ULL;
  static constexpr uint64_t REMLOCK   =0x8000000000000000ULL;

  struct Data{
    
    struct SharedData{
      alignas(ALIGNMENT) std::atomic_uint64_t D=UNASSIGNED;
    };

    alignas(ALIGNMENT) std::atomic_uint64_t   mExclusiveCounter=1;
    std::vector<SharedData>                   mSharedCounters;

    inline size_t assign(){
      //Find the first empty share counter slot. 
      for(size_t i=0;i<mSharedCounters.size();i++){
        auto &counter=mSharedCounters[i].D;

        uint64_t cmp=counter.load();
        if(cmp==UNASSIGNED&&counter.compare_exchange_strong(cmp,IDLE))
          return i;
      }

      std::runtime_error("RCUManager: Failed to find a free mutex");
    }

    inline void unassign(size_t idx){
      mSharedCounters[idx].D.store(UNASSIGNED);
    }

    Data(size_t size):mSharedCounters(size){}
  };

private:
  Data mCounters;

public:
  class Mutex{
    friend class RCUManager;
  private:

    Data                 *mCounters;
    uint64_t              mIdx;
    std::atomic_uint64_t &sCounter;

  protected:
    Mutex(Data *d,uint64_t idx):
      mCounters(d),
      mIdx(idx),
      sCounter(mCounters->mSharedCounters[idx].D){}

    inline void Wait(uint64_t exg){
      //Check all the shared counters to make sure they match the value of the exclusive. 
      for(auto &counter:mCounters->mSharedCounters){
        uint64_t backOff=2;
        
        while(counter.D.load(std::memory_order_relaxed)<exg){
          //If a counter isn't up to date yet. 
          //Wait a few extra cycles to avoid hammering cache
          for(uint64_t i=0;i<backOff;i++)
            //The fence is used to keep compilers from optimizing the for loop away
            std::atomic_thread_fence(std::memory_order_relaxed);

          backOff<<=1;
        };
      }
    }
  public:
    Mutex(const Mutex&)=delete;

    Mutex(Mutex&& r):
      mCounters(r.mCounters),
      mIdx(r.mIdx),
      sCounter(r.sCounter){

      r.mCounters=nullptr;
    }

    ~Mutex(){
      if(mCounters!=nullptr)
        mCounters->unassign(mIdx);
    }

    inline void lock(){
      uint64_t cmp;
      uint64_t exg;
      uint64_t backOff=2;
      uint64_t last=0;

      //The exclusive locking procoess works by trying to increment the exclusive counter, and
      //set the MSB of the counter as a flag to indicate that the lock is in use. 

      auto &eCounter=mCounters->mExclusiveCounter;

      cmp=eCounter.load()&~REMLOCK;

      //Test MSB and increment
      do{
        exg=cmp+INCLOCK; 
        if(eCounter.compare_exchange_strong(cmp,exg)){
          sCounter.store(exg,std::memory_order_release);
          break;
        }

        //Make sure the shared counter held by this mutex is always up to date, 
        //with out constanly invalidating the cache line used by the counter. 
        if(last<exg){
          last=exg;
          sCounter.store(exg,std::memory_order_release);
        }


        for(uint64_t i=0;i<backOff;i++)
          std::atomic_thread_fence(std::memory_order_relaxed);

        backOff<<=1;

        cmp=eCounter.load(std::memory_order_consume)&~REMLOCK;
      } while(true);

      Wait(exg);
    }

    inline bool try_lock(){
      uint64_t cmp;
      uint64_t exg;

      auto &eCounter=mCounters->mExclusiveCounter;

      cmp=eCounter.load()&~REMLOCK;
      exg=cmp+INCLOCK;

      if(eCounter.compare_exchange_strong(cmp,exg)){
        sCounter.store(exg);

        Wait(exg);

        return true;
      }
      return false;
    }

    inline void unlock(){
      sCounter.store(IDLE);
      //Clear the MSB of the counter
      mCounters->mExclusiveCounter.fetch_sub(REMLOCK);
    }

    inline void lock_shared(){
      auto &eCounter=mCounters->mExclusiveCounter;
      uint64_t cmp=eCounter.load();

      sCounter.store(cmp,std::memory_order_release);

      //Test for the MSB
      uint64_t backOff=2;
      while(cmp>REMLOCK){
        for(uint64_t i=0;i<backOff;i++)
          std::atomic_thread_fence(std::memory_order_relaxed);

        backOff<<=1;

        cmp=eCounter.load();
        sCounter.store(cmp,std::memory_order_release);
      }
    }

    inline bool try_lock_shared(){
      uint64_t cmp=mCounters->mExclusiveCounter.load();
      if(cmp>REMLOCK)
        return false;

      sCounter.store(cmp,std::memory_order_release);
      return true;
    }

    inline void unlock_shared(){
      sCounter.store(IDLE);
    }
  };

  RCUManager(size_t size):mCounters(size){};

  Mutex GetMutex(){
    uint64_t idx;

    idx=mCounters.assign();

    return std::move(Mutex(&mCounters,idx));
  }
};

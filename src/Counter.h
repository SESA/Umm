#ifndef UMM_COUNTER_H
#define UMM_COUNTER_H

#include <list>
#include <string>
#include <ebbrt/native/Debug.h>
#include <ebbrt/native/Perf.h>
#include "umm-common.h"
namespace umm{
namespace count{

// class TimeRecord;
class Counter {
public:
  class TimeRecord {
  public:
    TimeRecord(std::string s, uint64_t cyc, uint64_t ins, uint64_t ref)
      : s_(s), cycles_(cyc), ins_(ins), ref_cycles_(ref) {}
    std::string s_;
    uint64_t cycles_;
    uint64_t ins_;
    uint64_t ref_cycles_;
    float cyc_per_ins_;
  };

  void start_all() {
    cycles.Start();
    ins.Start();
    ref_cycles.Start();
  }

  void stop_all() {
    cycles.Stop();
    ins.Stop();
    ref_cycles.Stop();
  }

  void clear_all() {
    cycles.Clear();
    ins.Clear();
    ref_cycles.Clear();
  }

  void init_ctrs() {
    if(init_done_){
      ebbrt::kprintf_force("Trying to re-init counters\n");
      ebbrt::kabort();
    }
    init_done_ = true;

    cycles = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::cycles);
    ins = ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::instructions);
    ref_cycles =
        ebbrt::perf::PerfCounter(ebbrt::perf::PerfEvent::reference_cycles);

    stop_all();
    clear_all();
    start_all();
  }

  void reset_all() {
    stop_all();
    clear_all();
  }

  void dump_list(std::list<TimeRecord> ctr_list) {
    uint64_t cycles_total = 0;
    uint64_t ins_total = 0;
    uint64_t ref_total = 0;

    for (const auto &e : ctr_list) {
      cycles_total += e.cycles_;
      ins_total += e.ins_;
      ref_total += e.ref_cycles_;
    }

    int devisor = 10000;
    ebbrt::kprintf_force(YELLOW "TABLE: cycles and instructions x%d\n" RESET, devisor);
    for (const auto &e : ctr_list) {
      ebbrt::kprintf_force(CYAN "%10s:\tCyc:%6llu\tIns:%6llu\tRef:%6llu\tIns/"
                                "Cyc:%6.3f\tCyc\%:%3.3f%\n" RESET,
                           e.s_.c_str(), e.cycles_ / devisor, e.ins_ / devisor,
                           e.ref_cycles_ / devisor,
                           (float)e.ins_ / (float)e.cycles_,
                           100 * (float)e.cycles_ / (float)cycles_total);
    }
    ebbrt::kprintf_force(RESET "totals:" CYAN "\t\t\t %lu \t\t%lu \t\t%lu \n" RESET,
           cycles_total / devisor, ins_total / devisor, ref_total / devisor);
  }

  void add_to_list(std::list<TimeRecord>& ctr_list, TimeRecord r) {
    if(!init_done_){
      ebbrt::kprintf_force("Trying to add to list before init, skipping\n");
      return;
    }
    // ebbrt::kprintf_force(RED "add!!!\n" RESET);

    // Subract old from current.
    r.cycles_ = cycles.Read() - r.cycles_;
    r.ins_ = ins.Read() - r.ins_;
    r.ref_cycles_ = ref_cycles.Read() - r.ref_cycles_;

    // ebbrt::kprintf_force(RED "size was %d\n" RESET, ctr_list.size());
    ctr_list.emplace_back(r);
    // ebbrt::kprintf_force(RED "size now %d\n" RESET, ctr_list.size());

  }

  // void print_ctrs() {
  //   stop_all();
  //   // kprintf_force( RED "Run %d\t", ++inv_num);

  //   ebbrt::kprintf_force(
  //       CYAN "Cyc:%llu \t Ins:%llu \t Ref:%llu \t Ins/Cyc:%f\%\n" RESET,
  //       cycles.Read() / 100000, ins.Read() / 100000, ref_cycles.Read() / 10000,
  //       100 * (float)ins.Read() / (float)cycles.Read());
  //   start_all();
  // }

  // TimeRecord& CreateTimeRecord(std::string str) {
  //   auto r = new TimeRecord(*this, str);
  //   return *r;
  // }
  TimeRecord CreateTimeRecord(std::string str) {
    return TimeRecord(str, cycles.Read(), ins.Read(), ref_cycles.Read());
  }

private:
  bool init_done_ = false;
  ebbrt::perf::PerfCounter cycles;
  ebbrt::perf::PerfCounter ins;
  ebbrt::perf::PerfCounter ref_cycles;
};


}
}

#endif

/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <time.h>
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"
#include "ramulator_mem_ctrl.h"
#include "log.h"

#ifdef _WITH_RAMULATOR_
#include "ramulator/ZsimWrapper.h"
#include "ramulator/Request.h"
#include "scheduler.h"
#include <cassert>
#include <iostream>

class RamulatorAccEvent : public TimingEvent {
  private:
    RamulatorMemory* dram;
    bool write;
    Address addr;

  public:
    uint64_t sCycle;

    RamulatorAccEvent(RamulatorMemory* _dram, bool _write, Address _addr, int32_t _domain) 
        :  TimingEvent(0, 0, _domain), dram(_dram), write(_write), addr(_addr) {}
    

    bool isWrite() const {
        return write;
    }

    Address getAddr() const {
        return addr;
    }

    void simulate(uint64_t startCycle) {
        sCycle = startCycle;
        dram->enqueue(this, startCycle);
    }
};

RamulatorMemory::RamulatorMemory(uint32_t _minLatency, uint32_t _domain, const g_string& _name){
  curCycle = 0;
  minLatency = _minLatency;
  domain = _domain;
  name = _name;

  //callback
  cb_func = std::bind(&RamulatorMemory::readComplete, this, std::placeholders::_1);

  TickEvent<RamulatorMemory>* tickEv = new TickEvent<RamulatorMemory>(this, domain);
  tickEv->queue(0);  // start the sim at time 0

  // futex_init(&access_lock);
  // futex_init(&cb_lock);
  // futex_init(&enqueue_lock);
}

void RamulatorMemory::initStats(AggregateStat* parentStat) {
  AggregateStat* memStats = new AggregateStat();
  memStats->init(name.c_str(), "Memory controller stats");
  profReads.init("rd", "Completed Read requests"); memStats->append(&profReads);
  profWrites.init("wr", "Completed Write requests"); memStats->append(&profWrites);
  profTotalRdLat.init("rdlat", "Total latency experienced by completed read requests"); memStats->append(&profTotalRdLat);
  profTotalWrLat.init("wrlat", "Total latency experienced by completed write requests"); memStats->append(&profTotalWrLat);
  parentStat->append(memStats);
}

inline uint64_t RamulatorMemory::access(MemReq& req) {
  switch (req.type) {
      case PUTS:
      case PUTX:
          *req.state = I;
          break;
      case GETS:
          *req.state = req.is(MemReq::NOEXCL)? S : E;
          break;
      case GETX:
          *req.state = M;
          break;

      default: panic("!?");
  }

  Address addr = req.lineAddr << lineBits;
  uint64_t respCycle = req.cycle + minLatency;
  assert(respCycle > req.cycle);
  if ((req.type != PUTS /*discard clean writebacks*/) && zinfo->eventRecorders[req.srcId]) {
    bool isWrite = (req.type == PUTX);
    // assert(addr >= 0 && addr < memCapacity);
    RamulatorAccEvent* memEv = new (zinfo->eventRecorders[req.srcId]) RamulatorAccEvent(this, isWrite, addr, domain);
    memEv->setMinStartCycle(req.cycle);
    // futex_lock(&access_lock);
    TimingRecord tr = {addr, req.cycle, respCycle, req.type, memEv, memEv};
    zinfo->eventRecorders[req.srcId]->pushRecord(tr);
    // futex_unlock(&access_lock);
  }
  return respCycle;
}

uint32_t RamulatorMemory::tick(uint64_t cycle) {
    curCycle++;
    zinfo->ramulatorWrapper->tick();
    return 1;
}

void RamulatorMemory::readComplete(ramulator::Request& req) {
  // if(req.clflush){
  //     return;
  // }
  // futex_lock(&cb_lock);
  std::map<uint64_t, RamulatorAccEvent*>::iterator it = inflightRequests.find(uint64_t(req.addr));
  assert((it != inflightRequests.end()));
  RamulatorAccEvent* ev = it->second;
  // printf("Finished mem req at 0x%x\n", ev->getAddr());
  uint32_t lat = curCycle + 1 - ev->sCycle;

  // uint32_t core_id = req.pu_id;
  // if (!req.is_pim_inst)
  // {
  //     core_id = req.coreid;
  // }
  // zinfo->cores[core_id]->incMemAccLat(lat, req.acc_type);

  if (ev->isWrite()){
    profWrites.inc();
    profTotalWrLat.inc(lat);
  }else{
    profReads.inc();
    profTotalRdLat.inc(lat);
  }

  inflightRequests.erase(it);
  // futex_unlock(&cb_lock);
  ev->release();

  // printf("Memacc done @ domain %d cycle %d\n",ev->getDomain(), curCycle+1);
  ev->done(curCycle+1);
}

void RamulatorMemory::enqueue(RamulatorAccEvent* ev, uint64_t cycle) {
    // info("[%s] %s access to %lx added at %ld, %ld inflight reqs", getName(), ev->isWrite()? "Write" : "Read", ev->getAddr(), cycle, inflightRequests.size());
  ramulator::Request::Type req_type;
  if(ev->isWrite())
    req_type = ramulator::Request::Type::WRITE;
  else
    req_type = ramulator::Request::Type::READ;

  int core_id = 0;

  Address ramulator_addr = ev->getAddr();
  
  // futex_lock(&enqueue_lock);
  ramulator::Request req(ramulator_addr, req_type, cb_func, core_id);

  if(zinfo->ramulatorWrapper->send(req)){

    // printf("Issuing memacc to Ramulator @ domain %d cycle %d\n",ev->getDomain(), curCycle+1);
    inflightRequests.insert(std::pair<uint64_t, RamulatorAccEvent*>(req.addr, ev));
    ev->hold();
  }else{
      ev->requeue(cycle+1);
  }
  // futex_unlock(&enqueue_lock);
}
#else
#endif
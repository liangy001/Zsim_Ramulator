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

#ifndef RAMULATOR_MEM_CTRLS_H_
#define RAMULATOR_MEM_CTRLS_H_

#include <map>
#include <deque>
#include <string>
#include <functional>
#include "g_std/g_string.h"
#include "g_std/g_multimap.h"
#include "memory_hierarchy.h"
#include "pad.h"
#include "stats.h"
#include "locks.h"


namespace ramulator {
    class Request;
}

class RamulatorAccEvent;

class RamulatorMemory : public MemObject {
    private:
        g_string name;
        uint32_t minLatency;
        uint32_t domain;

        g_map<uint64_t, RamulatorAccEvent*> inflightRequests;//<addr, ev*>

        // lock_t access_lock;
        // lock_t enqueue_lock;
        // lock_t cb_lock;

        uint64_t curCycle;
        // R/W stats
        PAD();
        Counter profReads;
        Counter profWrites;
        Counter profTotalRdLat;
        Counter profTotalWrLat;
        PAD();

    public:
        RamulatorMemory(uint32_t _minLatency, uint32_t _domain, const g_string& _name);

        const char* getName() {return name.c_str();}
        void initStats(AggregateStat* parentStat);

        inline uint64_t access(MemReq& req);
        uint32_t tick(uint64_t cycle);
        void enqueue(RamulatorAccEvent* ev, uint64_t cycle);

        // void getStats(MemStats &stat);

    private:
        void readComplete(ramulator::Request& req);
        std::function<void(ramulator::Request&)> cb_func;
};

class SplitAddrMemory : public MemObject
{
  private:
    const g_vector<MemObject *> mems;
    const g_string name;

  public:
    SplitAddrMemory(const g_vector<MemObject *> &_mems, const char *_name) : mems(_mems), name(_name) {}

    uint64_t access(MemReq &req)
    {
        Address addr = req.lineAddr;
        uint32_t mem = addr % mems.size();
        Address ctrlAddr = addr / mems.size();
        req.lineAddr = ctrlAddr;
        uint64_t respCycle = mems[mem]->access(req);
        req.lineAddr = addr;
        return respCycle;
    }

    const char *getName()
    {
        return name.c_str();
    }

    void initStats(AggregateStat *parentStat)
    {
        for (auto mem : mems)
            mem->initStats(parentStat);
    }
};

#endif  // RAMULATOR_MEM_CTRLS_H_
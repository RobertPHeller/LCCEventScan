// -!- c++ -!- //////////////////////////////////////////////////////////////
//
//  System        : 
//  Module        : 
//  Object Name   : $RCSfile$
//  Revision      : $Revision$
//  Date          : $Date$
//  Author        : $Author$
//  Created By    : Robert Heller
//  Created       : Wed Sep 4 12:44:50 2024
//  Last Modified : <260305.1531>
//
//  Description	
//
//  Notes
//
//  History
//	
/////////////////////////////////////////////////////////////////////////////
//
//    Copyright (C) 2024  Robert Heller D/B/A Deepwoods Software
//			51 Locke Hill Road
//			Wendell, MA 01379-9728
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// 
//
//////////////////////////////////////////////////////////////////////////////

#ifndef __NETWORKHEALTHSCAN_HXX
#define __NETWORKHEALTHSCAN_HXX

#include "openlcb/EventHandlerTemplates.hxx"
#include "openlcb/ConfigRepresentation.hxx"
#include "utils/ConfigUpdateListener.hxx"
#include "utils/ConfigUpdateService.hxx"
#include "openlcb/RefreshLoop.hxx"
#include "openlcb/SimpleStack.hxx"
#include "executor/Timer.hxx"
#include "executor/Notifiable.hxx"
#include "openlcb/NodeBrowser.hxx"
#include "openlcb/SNIPClient.hxx"
#include <stdio.h>
#include <stdlib.h>
#include "utils/logging.h"
#include <string>
#include <map>
#include <algorithm>
#include <functional>

namespace NetworkHealthScan
{
struct NetworkNodeDatabaseEntry {
    typedef enum {Missing=0, Found, New} Status_t;
    const openlcb::NodeID node_id;
    string manufacturer;
    string model;
    string softwareVersion;
    string hardwareVersion;
    string name;
    string description;
    Status_t status;
    NetworkNodeDatabaseEntry(openlcb::NodeID node_id_=0,
                             string manufacturer_="",
                             string model_="",
                             string softwareVersion_="",
                             string hardwareVersion_="",
                             string name_="",
                             string description_="",
                             Status_t status_=Missing)
                : node_id(node_id_)
          , manufacturer(manufacturer_)
          , model(model_)
          , softwareVersion(softwareVersion_)
          , hardwareVersion(hardwareVersion_)
          , name(name_)
          , description(description_)
          , status(status_)
    {
    }
    NetworkNodeDatabaseEntry(const NetworkNodeDatabaseEntry& other)
                : node_id(other.node_id)
          , manufacturer(other.manufacturer)
          , model(other.model)
          , softwareVersion(other.softwareVersion)
          , hardwareVersion(other.hardwareVersion)
          , name(other.name)
          , description(other.description)
          , status(other.status)
    {
    }
};



class NetworkHealthScan : public Timer
{
private:
public:
    NetworkHealthScan(openlcb::Node *node, Service *service, 
                      ActiveTimers *timers)
                : Timer(timers)
          , node_(node)
          , service_(service)
          , browsehandleflow_(node,service,this)
          , needWriteDB_(false)
          , currentState_(Init)
          , added_(0)
          , missing_(0)
          , found_(0)
    {
        ReadDB_();
    }
    openlcb::MemorySpace *NodeDBSpace()
    {
        return new openlcb::ROFileMemorySpace(NODEDB);
    }
    ~NetworkHealthScan() 
    {
    }
    virtual void notify() override {} // empty norify for now.
    void ResetNodeDB();
    void ScanNetwork();
    typedef std::map<openlcb::NodeID,NetworkNodeDatabaseEntry> NodeDB_t;
    typedef NodeDB_t::const_iterator NodeDB_ConstIterator;
    typedef NodeDB_t::iterator NodeDB_Iterator;
    NodeDB_ConstIterator NodeDB_Begin() const {return NodeDB_.begin();}
    NodeDB_ConstIterator NodeDB_End() const {return NodeDB_.end();}
    NodeDB_Iterator NodeDB_Find(openlcb::NodeID nodeid) {return NodeDB_.find(nodeid);}
    typedef enum {Init=0, Scanning, ScanComplete} ScanState_t;
    ScanState_t CurrentState() const {return currentState_;}
    size_t Total() const {return NodeDB_.size();}
    size_t Added() const {return added_;}
    size_t Missing() const {return missing_;}
    size_t Found() const {return found_;}
    inline void insertDB(openlcb::NodeID nid,NetworkNodeDatabaseEntry entry)
    {
        NodeDB_.insert(std::make_pair(nid,entry));
    }
private:
    static constexpr const char NODEDB[] = "/sdcard/nodedb";
    static constexpr const long long BROWSETIMEOUT = MSEC_TO_NSEC(20000);
    NodeDB_t NodeDB_;
    void ReadDB_();
    void WriteDB_();
    long long timeout() override;
    openlcb::Node *node_;
    Service *service_;
    class BrowseHandleFlow : public StateFlowBase
    {
    public:
        BrowseHandleFlow(openlcb::Node *node, Service *service,
                         NetworkHealthScan *parent)
                    : StateFlowBase(service)
              , node_(node)
              , parent_(parent)
              , busy_(false)
              , browser_(node, std::bind(&BrowseHandleFlow::browseCallback_, 
                                         this,std::placeholders::_1))
              , snipProcess_(service, parent, &busy_)
        {
            start_flow(STATE(entry));
        }
        virtual void notify() override
        {
            LOG(INFO,"[BrowseHandleFlow] notify()");
            StateFlowBase::notify();
        }
        void refresh() {browser_.refresh();}
    private:
        void browseCallback_(openlcb::NodeID nodeid);
        openlcb::Node *node_;
        NetworkHealthScan *parent_;
        bool busy_;
        openlcb::NodeBrowser browser_;
        struct PendingNodeID : public QMember
        {
            PendingNodeID(openlcb::NodeID nodeid)
            {
                this->nodeid = nodeid;
            }
            openlcb::NodeID nodeid;
        };
        Q pendingNodeIDs_;
        struct GetSNIP {
            openlcb::Node *src;
            openlcb::NodeHandle dst;
            void reset(openlcb::Node *src, openlcb::NodeHandle dst)
            {
                this->src = src;
                this->dst = dst;
            }
        };
        typedef StateFlow<Buffer<GetSNIP>, QList<1>> SNIPProcessBase;
        class SNIPProcess : public SNIPProcessBase
        {
        public:
            SNIPProcess(Service *service, NetworkHealthScan *parent, 
                        bool *busy)
                        : SNIPProcessBase(service)
                  , client_(service)
                  , parent_(parent)
                  , busy_(busy)
                  , buffer_(nullptr)
            {
            }
            virtual ~SNIPProcess()
            {
            }
#if 0
            virtual void notify() override
            {
                LOG(INFO,"[NetworkHealthScan] SNIPProcess::notify()");
                SNIPProcessBase::notify();
            }
#endif
        private:
            openlcb::SNIPClient client_;
            NetworkHealthScan *parent_;
            bool *busy_;
            Buffer<openlcb::SNIPClientRequest> *buffer_;
            virtual Action entry();
            Action startSNIP();
            Action gotSNIP();
        };
        class SNIPHelper : public Executable
        {
        public:
            SNIPHelper()
            {
            }
            void SNIPAsync(SNIPProcess *flow,openlcb::Node *src, 
                           openlcb::NodeHandle dst, Notifiable *done)
            {
                LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow::SNIPHelper] SNIPAsync()");
                done_.reset(done);
                src_ = src;
                dst_ = dst;
                flow_ = flow;
                flow_->alloc_async(this);
            }
        private:
            SNIPProcess *flow_;
            openlcb::Node *src_;
            openlcb::NodeHandle dst_;
            // Callback from the allocator.
            void alloc_result(QMember *entry) override
            {
                LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow::SNIPHelper] alloc_result()");
                Buffer<GetSNIP> *b = flow_->cast_alloc(entry);
                b->data()->reset(src_,dst_);
                b->set_done(&done_);
                flow_->send(b);
            }
            void run() override
            {
                HASSERT(0);
            }
            BarrierNotifiable done_;
        };
        SNIPProcess snipProcess_;
        SNIPHelper snipHelper;
        virtual Action entry();
        Action gotSNIP();
    };
    BrowseHandleFlow browsehandleflow_;
    BarrierNotifiable bn_;
    bool needWriteDB_;
    openlcb::WriteHelper write_helpers[3];
    ScanState_t currentState_;
    size_t added_;
    size_t missing_;
    size_t found_;
};
          
          
}
#endif // __NETWORKHEALTHSCAN_HXX


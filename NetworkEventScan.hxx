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
//  Last Modified : <260308.2225>
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
#include "openlcb/PIPClient.hxx"
#include "openlcb/MemoryConfigClient.hxx"
#include <stdio.h>
#include <stdlib.h>
#include "utils/logging.h"
#include <string>
#include <map>
#include <algorithm>
#include <functional>
#include <csv.h>
#include <libxml++/libxml++.h>

namespace NetworkEventScan
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



class NetworkEventScan : public StateFlowBase
{
private:
public:
    NetworkEventScan(openlcb::Node *node, Service *service, 
                     openlcb::MemoryConfigHandler *memCfgHandler,
                     const char *filename)
          : StateFlowBase(service)
          , timer_(this)
    , node_(node)
    , service_(service)
    , browsehandleflow_(node,service,this)
    , memClient_(node,memCfgHandler)
    , filename_(filename)
    , currentState_(Init)
    , added_(0)
    , missing_(0)
    , found_(0)
    {
    }
    ~NetworkEventScan() 
    {
    }
    void ScanNetwork()
    {
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan] ScanNetwork()");
#endif
        start_flow(STATE(entry));
    }
    virtual Action entry();
    Action node_loop_start();
    Action start_load_CDI();
    Action gotCDIBlock();
    Action gotCDI();
    Action NextNode();
    typedef std::map<openlcb::NodeID,NetworkNodeDatabaseEntry> NodeDB_t;
    typedef NodeDB_t::const_iterator NodeDB_ConstIterator;
    typedef NodeDB_t::iterator NodeDB_Iterator;
    NodeDB_ConstIterator NodeDB_Begin() const {return NodeDB_.begin();}
    NodeDB_ConstIterator NodeDB_End() const {return NodeDB_.end();}
    NodeDB_Iterator NodeDB_Find(openlcb::NodeID nodeid) {return NodeDB_.find(nodeid);}
    void NodeDB_Remove(openlcb::NodeID nodeid)
    {
        auto found = NodeDB_.find(nodeid);
        if (found != NodeDB_.end())
        {
            NodeDB_.erase(found);
        }
    }
    NodeDB_ConstIterator currentNode_;
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
    static constexpr const long long BROWSETIMEOUT = MSEC_TO_NSEC(20000);
    NodeDB_t NodeDB_;
    StateFlowTimer timer_;
    openlcb::Node *node_;
    Service *service_;
    class BrowseHandleFlow : public StateFlowBase
    {
    public:
        BrowseHandleFlow(openlcb::Node *node, Service *service,
                         NetworkEventScan *parent)
                    : StateFlowBase(service)
              , node_(node)
              , parent_(parent)
              , busy_(false)
              , browser_(node, std::bind(&BrowseHandleFlow::browseCallback_, 
                                         this,std::placeholders::_1))
              , snipProcess_(service, parent, &busy_)
              , pipClient_(node->iface())
        {
            start_flow(STATE(entry));
        }
        virtual void notify() override
        {
#ifdef DEBUG
            LOG(INFO,"[BrowseHandleFlow] notify()");
#endif
            StateFlowBase::notify();
        }
        void refresh() {browser_.refresh();}
    private:
        void browseCallback_(openlcb::NodeID nodeid);
        openlcb::Node *node_;
        NetworkEventScan *parent_;
        bool busy_;
        openlcb::NodeID nodeid_;
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
        typedef StateFlow<Buffer<GetSNIP>, QList<4>> SNIPProcessBase;
        class SNIPProcess : public SNIPProcessBase
        {
        public:
            SNIPProcess(Service *service, NetworkEventScan *parent, 
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
#ifdef DEBUG
                LOG(INFO,"[NetworkEventScan] SNIPProcess::notify()");
#endif
                SNIPProcessBase::notify();
            }
#endif
        private:
            openlcb::SNIPClient client_;
            NetworkEventScan *parent_;
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
#ifdef DEBUG
                LOG(INFO,"[NetworkEventScan::BrowseHandleFlow::SNIPHelper] SNIPAsync()");
#endif
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
#ifdef DEBUG
                LOG(INFO,"[NetworkEventScan::BrowseHandleFlow::SNIPHelper] alloc_result()");
#endif
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
        openlcb::PIPClient pipClient_;
        virtual Action entry();
        Action gotSNIP();
        Action gotPIP();
    };
    static const char * headings_[10];
    static constexpr size_t NUM_COLUMNS = (sizeof(headings_) / sizeof(headings_[0]));;
    void writeHeadings_()
    {
        for (size_t i=0;i < NUM_COLUMNS; i++)
        {
            if (i > 0) fputc(',',outfp_);
            csv_fwrite(outfp_,headings_[i],strlen(headings_[i]));
        }
        fputc('\n',outfp_);
    }
    BrowseHandleFlow browsehandleflow_;
    openlcb::MemoryConfigClient memClient_;
    Buffer<openlcb::MemoryConfigClientRequest> *MEMBuffer_{nullptr};
    string CDI_;
    unsigned CDI_Offset;
    static constexpr uint8_t CDISPACE = 0xff;
    static constexpr unsigned CDIBLOCKSIZE = 1024;
    std::string filename_;
    FILE *outfp_;
    string lastTextBeforeEV_;
    void processNode_(const xmlpp::Node* n,int space,uint32_t &address,string prefix="");
    uint32_t segmentnumber_;
    BarrierNotifiable bn_;
    openlcb::WriteHelper write_helpers[3];
    ScanState_t currentState_;
    size_t added_;
    size_t missing_;
    size_t found_;
};
          
          
}
#endif // __NETWORKHEALTHSCAN_HXX


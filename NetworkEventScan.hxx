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
//  Last Modified : <260316.0924>
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
#include <stack>
#include <csv.h>
#include <libxml++/libxml++.h>


/** Namespace to hold all of the working code. */
namespace NetworkEventScan
{
/** Data structure that holds the SNIP information for a node.
 */
struct NetworkNodeDatabaseEntry {
    typedef enum {Missing=0, Found, New} Status_t;
    /** The node id. */
    const openlcb::NodeID node_id;
    /** The manufacturer of the node. */
    string manufacturer;
    /** The model of the node. */
    string model;
    /** The software version. */
    string softwareVersion;
    /** The hardware version. */
    string hardwareVersion;
    /** The user supplied name of the node. */
    string name;
    /** The user supplied description of the node. */ 
    string description;
    /** The node's status. */
    Status_t status;
    /** Default constructor, with default values.
     * @param node_id_ The node id.
     * @param manufacturer_ The manufacturer of the node.
     * @param model_ The model of the node.
     * @param softwareVersion_ The software version.
     * @param hardwareVersion_ The hardware version. 
     * @param name_ The user supplied name of the node.
     * @param description_ The user supplied description of the node.
     * @param status_ The node's status.
     */
       
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
    /** Copy constructor.  Create a new entry from another entry.
     * @param other The other entry.
     */
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

/** Main network event scanner.
 * This class is the workhorse of the application.  It uses a collection of
 * internal sub-classes to scan the network and create the result file.
 */
class NetworkEventScan : public StateFlowBase
{
public:
    /** Constructor: create a NetworkEventScan instance.
     * @param node The node object.
     * @param service The service to run under.
     * @param memCfgHandler The memory config handler.
     * @param filename The file name to write the results to.
     */
    NetworkEventScan(openlcb::Node *node, Service *service, 
                     openlcb::MemoryConfigHandler *memCfgHandler,
                     const char *filename)
          : StateFlowBase(service)
          , timer_(this)
    , node_(node)
    , browsehandleflow_(node,service,this)
    , memClient_(node,memCfgHandler)
    , filename_(filename)
    , currentState_(Init)
    , added_(0)
    , missing_(0)
    , found_(0)
    {
    }
    /** Destructor: clean up a NetworkEventScan instance. */
    ~NetworkEventScan() 
    {
    }
    /** Initiate a scan. */ 
    void ScanNetwork()
    {
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan] ScanNetwork()");
#endif
        start_flow(STATE(entry));
    }
    /** Entry state for the state machine */
    virtual Action entry();
    /** Start the node loop. */
    Action node_loop_start();
    /** Start loading the CDI */
    Action start_load_CDI();
    /** Got a block of CDI memory. */
    Action gotCDIBlock();
    /** Got the complete CDI. */
    Action gotCDI();
    /** Move on to the next node. */
    Action NextNode();
    /** Map of nodes. */
    typedef std::map<openlcb::NodeID,NetworkNodeDatabaseEntry> NodeDB_t;
    /** Const iterator into the map of nodes. */
    typedef NodeDB_t::const_iterator NodeDB_ConstIterator;
    /** Iterator into the map of nodes. */
    typedef NodeDB_t::iterator NodeDB_Iterator;
    /** Beginning of the node map. */
    NodeDB_ConstIterator NodeDB_Begin() const {return NodeDB_.begin();}
    /** End of the node map. */
    NodeDB_ConstIterator NodeDB_End() const {return NodeDB_.end();}
    /** Find a node by its node id. */
    NodeDB_Iterator NodeDB_Find(openlcb::NodeID nodeid) {return NodeDB_.find(nodeid);}
    /** Remove a node by its id. */
    void NodeDB_Remove(openlcb::NodeID nodeid)
    {
        auto found = NodeDB_.find(nodeid);
        if (found != NodeDB_.end())
        {
            NodeDB_.erase(found);
        }
    }
    /** Scan states, */
    typedef enum {Init=0, Scanning, ScanComplete} ScanState_t;
    /** Return the current scan state. */
    ScanState_t CurrentState() const {return currentState_;}
    /** Return the total number of nodes. */
    size_t Total() const {return NodeDB_.size();}
    /** Insert a node into the map. */
    inline void insertDB(openlcb::NodeID nid,NetworkNodeDatabaseEntry entry)
    {
        NodeDB_.insert(std::make_pair(nid,entry));
    }
private:
    /** The node we are currently working on. */
    NodeDB_ConstIterator currentNode_;
    static constexpr const long long BROWSETIMEOUT = MSEC_TO_NSEC(20000);
    NodeDB_t NodeDB_;
    StateFlowTimer timer_;
    openlcb::Node *node_;
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
#ifdef DEBUG
        virtual void notify() override
        {
            LOG(INFO,"[BrowseHandleFlow] notify()");
            StateFlowBase::notify();
        }
#endif
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
    xmlpp::DomParser parser_;
    uint32_t address_{0};
    unsigned segmentnumber_{0};
    void processNode_(xmlpp::Node const* n,int space,string prefix);
#if 0
    struct ProcessNodeMessage : public CallableFlowRequestBase {
        xmlpp::Node const* rootNode;
        FILE *outfp;
        void reset(xmlpp::Node const* rootNode,FILE *outfp)
        {
            this->rootNode = rootNode;
            this->outfp = outfp;
        }
    };
    typedef StateFlow<Buffer<ProcessNodeMessage>, QList<1>> ProcessNodeBase;
    class ProcessNode : public ProcessNodeBase
    {
    public:
        ProcessNode(Service *service,
                    openlcb::MemoryConfigClient &memClient,
                    Buffer<openlcb::MemoryConfigClientRequest> *MEMBuffer)
                    : ProcessNodeBase(service)
              , memClient_(memClient)
              , MEMBuffer_(MEMBuffer)
              , address_(0)
              , segmentnumber_(0)
        {
        }
    private:
        xmlpp::Node const* rootNode_;
        openlcb::MemoryConfigClient &memClient_;
        Buffer<openlcb::MemoryConfigClientRequest> *MEMBuffer_;
        FILE *outfp_;
        uint32_t address_;
        unsigned segmentnumber_;
        typedef struct ProcessNodeStackElement {
            xmlpp::Node const* n;
            int space;
            string prefix;
            const xmlpp::Node::NodeList children;
            xmlpp::Node::NodeList::const_iterator child;
            ProcessNodeStackElement(xmlpp::Node const* n,
                                    int space,
                                    string prefix)
                        : n(n)
                  , space(space)
                  , prefix(prefix)
                  , children(xmlpp::Node::NodeList())
                  , child(children.end())
            {
            }
            ProcessNodeStackElement(xmlpp::Node const* n,
                                    int space,
                                    string prefix,
                                    const xmlpp::Node::NodeList children,
                                    const xmlpp::Node::NodeList::const_iterator child)
                        : n(n)
                  , space(space)
                  , prefix(prefix)
                  , children(children)
                  , child(child)
            {
            }
        } ProcessNodeStackElement_t;
        std::stack<ProcessNodeStackElement> processElementStack_;
        virtual Action entry();
        Action process();
    };
    ProcessNode processNode_;
#endif
    openlcb::WriteHelper write_helpers[3];
    ScanState_t currentState_;
    size_t added_;
    size_t missing_;
    size_t found_;
};

/** Class to hold a fresh thread to run the Network Event Scan in. */
class NetworkEventScanThread : public Service
{
public:
    /** Constructor: create a new thread to run the Network Event Scan in.
     * @param executor The thread object.
     * @param node The node object.
     * @param memCfgHandler The memory config handler.
     * @param filename The file name to write the results to.
     */ 
    NetworkEventScanThread(ExecutorBase *executor, openlcb::Node *node,
                           openlcb::MemoryConfigHandler *memCfgHandler,
                           const char *filename)
          : Service(executor)
    , networkEventScan_(node,this,memCfgHandler,filename)
    {
    }
    /** Destructor: clean things up. */
    ~NetworkEventScanThread()
    {
    }
private:
    NetworkEventScan networkEventScan_;
};

          
}
#endif // __NETWORKHEALTHSCAN_HXX


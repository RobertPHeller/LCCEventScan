// -!- C++ -!- //////////////////////////////////////////////////////////////
//
//  System        : 
//  Module        : 
//  Object Name   : $RCSfile$
//  Revision      : $Revision$
//  Date          : $Date$
//  Author        : $Author$
//  Created By    : Robert Heller
//  Created       : Wed Sep 4 14:31:24 2024
//  Last Modified : <260307.1523>
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

static const char rcsid[] = "@(#) : $Id$";

#include "openlcb/EventHandlerTemplates.hxx"
#include "openlcb/ConfigRepresentation.hxx"
#include "utils/ConfigUpdateListener.hxx"
#include "utils/ConfigUpdateService.hxx"
#include "StringUtils.hxx"
#include "openlcb/RefreshLoop.hxx"
#include "openlcb/SimpleStack.hxx"
#include "executor/Timer.hxx"
#include "executor/Notifiable.hxx"
#include "openlcb/NodeBrowser.hxx"
#include "openlcb/SNIPClient.hxx"
#include "openlcb/PIPClient.hxx"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "utils/logging.h"
#include <string>
#include <csv.h>
#include <libxml++/libxml++.h>

#include "NetworkEventScan.hxx"

namespace NetworkEventScan
{

StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::entry()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] entry()");
#endif
    if (!pendingNodeIDs_.empty())
    {
        PendingNodeID *temp = (PendingNodeID*) (pendingNodeIDs_.next().item);
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] entry(): temp = %p",temp);
#endif
        nodeid_ = temp->nodeid;
        delete temp;
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] entry(): pendingNodeIDs_ contains %ld items",pendingNodeIDs_.pending());
#endif
        auto found = parent_->NodeDB_Find(nodeid_);
        if (found == parent_->NodeDB_End())
        {
            snipHelper.SNIPAsync(&snipProcess_,node_,openlcb::NodeHandle(nodeid_),
                                 this);
            busy_ = true;
            return wait_and_call(STATE(gotSNIP));
        }
        else
        {
            return again();
        }
    }
    else if (parent_->CurrentState() == ScanComplete && !busy_)
    {
        return exit();
    }
    else
    {
        return wait();
    }
}

StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::gotSNIP()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] gotSNIP()]");
#endif
    pipClient_.request(openlcb::NodeHandle(nodeid_),node_,this);
    return wait_and_call(STATE(gotPIP));
}
StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::gotPIP()
{
    uint64_t protocols = pipClient_.response();
    if ((protocols & openlcb::Defs::Protocols::CDI) == 0L ||
        (protocols & openlcb::Defs::Protocols::EVENT_EXCHANGE) == 0L)
    {
        parent_->NodeDB_Remove(nodeid_);
    }        
    if (busy_) return wait();
    if (pendingNodeIDs_.empty())
    {
        return wait_and_call(STATE(entry));
    }
    else
    {
        return call_immediately(STATE(entry));
    }
}

void NetworkEventScan::BrowseHandleFlow::browseCallback_(openlcb::NodeID nodeid)
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(0x%012lX)",nodeid);
#endif
    if (nodeid == node_->node_id()) 
    {
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): parent_->CurrentState() is %d",parent_->CurrentState() );
#endif
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): busy_ is %d",busy_);
#endif
        if (parent_->CurrentState() == Init && !busy_)
        {
            parent_->ScanNetwork();
        }
        //if (!busy_) notify();
        return; // skip ourself
    }
#ifdef DEBUG
    //LOG(INFO,"[NetworkEventScan] browseCallback_(), currentState_ is %d",(int)currentState_);
#endif
    auto found = parent_->NodeDB_Find(nodeid);
    if (found == parent_->NodeDB_End())
    {
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): found == NodeDB_.end()");
#endif
        //SyncNotifiable n;
        //snipHelper.SNIPAsync(&snipProcess_,node_,openlcb::NodeHandle(nodeid),
        //                     this);
        //n.wait_for_notification();
        QMember *newitem = new PendingNodeID(nodeid);
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): newitem is %p",newitem);
#endif
        pendingNodeIDs_.insert(newitem);
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): pendingNodeIDs_ countains %ld elements",pendingNodeIDs_.pending());
#endif
        if (!busy_) notify();
    }
    else
    {
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan] browseCallback_(): found != NodeDB_.end()");
#endif
        found->second.status = NetworkNodeDatabaseEntry::Found;
    }
}


StateFlowBase::Action NetworkEventScan::entry()
{
#ifdef DEBUG
    LOG(INFO, "[NetworkEventScan] entry()");
#endif
    if (currentState_ == Scanning) return again();
    NodeDB_.clear();
    currentState_ = Scanning;
    browsehandleflow_.refresh();
    return sleep_and_call(&timer_, BROWSETIMEOUT, STATE(node_loop_start));

}

StateFlowBase::Action NetworkEventScan::node_loop_start()
{
#ifdef DEBUG
    LOG(INFO, "[NetworkEventScan] node_loop_start()");
#endif
    currentState_ = ScanComplete;
    for (auto n = NodeDB_.begin();n != NodeDB_.end(); n++)
    {
        openlcb::NodeID nid = n->first;
        NetworkNodeDatabaseEntry node = n->second;
//#ifdef DEBUG
        LOG(INFO, "[NetworkEventScan] node_loop_start(): 0X%012lX: manufacturer is '%s', model = '%s', softwareVersion = '%s', hardwareVersion = '%s', name = '%s', description = '%s'",nid,node.manufacturer.c_str(),node.model.c_str(),node.softwareVersion.c_str(),node.hardwareVersion.c_str(),node.name.c_str(),node.description.c_str());
//#endif
    }
    currentNode_ = NodeDB_.begin();
    if (currentNode_ == NodeDB_.end())
    {
        ::exit(1);
    }
    outfp_ = fopen(filename_.c_str(),"wb");
    writeHeadings_();
    return call_immediately(STATE(start_load_CDI));
}

const char * NetworkEventScan::headings_[10] = {
    "EventID",
    "EventName",
    "NodeID",
    "NodeName",
    "NodeModel",
    "Manufacturer",
    "GroupPath",
    "Description",
    "Role",
    "SuggestedJMRIType"
};


StateFlowBase::Action NetworkEventScan::start_load_CDI()
{
    
    fclose(outfp_);
    ::exit(0);
    return exit(); // place holder
}

StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::SNIPProcess::entry()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow::SNIPProcess::entry()]");
#endif
    return allocate_and_call(&client_,STATE(startSNIP));
}

StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::SNIPProcess::startSNIP()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow::SNIPProcess::startSNIP()]");
#endif
    GetSNIP *m = message()->data();
    buffer_ = get_allocation_result(&client_);
    buffer_->data()->reset(m->src,m->dst);
    buffer_->data()->done.reset(this);
    buffer_->ref();
    client_.send(buffer_);
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan::BrowseHandleFlow::SNIPProcess::startSNIP(): client_.send() called.");
#endif
    return wait_and_call(STATE(gotSNIP));
}

#ifdef DEBUG
static std::string HexDumpPayload(openlcb::Payload p)
{
    std::string result;
    char buffer[8];
    for (unsigned i = 0; i < p.size(); i++)
    {
        snprintf(buffer,sizeof(buffer),"0x%02x ",p[i]);
        result += buffer;
    }
    return result;
}
#endif

StateFlowBase::Action NetworkEventScan::BrowseHandleFlow::SNIPProcess::gotSNIP()
{
    openlcb::Payload &p = buffer_->data()->response;
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): p = %s",HexDumpPayload(p).c_str());
#endif
    string manufacturer("");
    string model("");
    string softwareVersion("");
    string hardwareVersion("");
    string name("");
    string description("");
    size_t index = 0;
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): buffer_->data()->resultCode is %d",buffer_->data()->resultCode);
#endif
    uint8_t version = p[index++];
    if (version > 4) version = 4;
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): version(1) = %d",version);
#endif
    for (size_t i=0; i<version; i++)
    {
        if (index > p.size()) break;
        char c = p[index++];
#ifdef DEBUG
        //LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): c(1) = '%c'",c);
#endif
        while (c != '\0')
        {
            switch (i)
            {
            case 0:
                manufacturer += c;
                break;
            case 1:
                model += c;
                break;
            case 2:
                softwareVersion += c;
                break;
            case 3:
                hardwareVersion += c;
                break;
            }
            c = p[index++];
        }
    }
    version = p[index++];
    if (version > 2) version = 2;
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): version(2) = %d",version);
#endif
    for (size_t i=0; i<version; i++)
    {
        if (index > p.size()) break;
        char c = p[index++];
        while (c != '\0')
        {
            switch (i)
            {
            case 0:
                name += c;
                break;
            case 1:
                description += c;
                break;
            }
            c = p[index++];
        }
    }
    GetSNIP *m = message()->data();
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] SNIPProcess::gotSNIP(): 0X%012lX: manufacturer is '%s', model = '%s', softwareVersion = '%s', hardwareVersion = '%s', name = '%s', description = '%s'",m->dst.id,manufacturer.c_str(),model.c_str(),softwareVersion.c_str(),hardwareVersion.c_str(),name.c_str(),description.c_str());
#endif
    parent_->insertDB(m->dst.id,
                      NetworkNodeDatabaseEntry(m->dst.id,manufacturer,
                                               model,softwareVersion,
                                               hardwareVersion,name,
                                               description,
                                               NetworkNodeDatabaseEntry::New));
    

    buffer_->unref();
    buffer_ = nullptr;
    *busy_ = false;
    return release_and_exit();
}

}

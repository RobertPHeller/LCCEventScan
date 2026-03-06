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
//  Last Modified : <260306.1216>
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
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include "utils/logging.h"
#include <string>
#include "NetworkHealthScan.hxx"

namespace NetworkHealthScan
{

StateFlowBase::Action NetworkHealthScan::BrowseHandleFlow::entry()
{
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] entry()");
    if (!pendingNodeIDs_.empty())
    {
        PendingNodeID *temp = (PendingNodeID*) (pendingNodeIDs_.next().item);
        LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] entry(): temp = %p",temp);
        openlcb::NodeID nodeid = temp->nodeid;
        delete temp;
        LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] entry(): pendingNodeIDs_ contains %ld items",pendingNodeIDs_.pending());
        auto found = parent_->NodeDB_Find(nodeid);
        if (found == parent_->NodeDB_End())
        {
            snipHelper.SNIPAsync(&snipProcess_,node_,openlcb::NodeHandle(nodeid),
                                 this);
            busy_ = true;
            return wait_and_call(STATE(gotSNIP));
        }
        else
        {
            return again();
        }
    }
    else
    {
        return wait();
    }
}

StateFlowBase::Action NetworkHealthScan::BrowseHandleFlow::gotSNIP()
{
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] gotSNIP()]");
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

void NetworkHealthScan::BrowseHandleFlow::browseCallback_(openlcb::NodeID nodeid)
{
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] browseCallback_(0x%012lX)",nodeid);
    if (nodeid == node_->node_id()) 
    {
        if (parent_->CurrentState() == Init && !busy_)
        {
            parent_->ScanNetwork();
        }
        //if (!busy_) notify();
        return; // skip ourself
    }
    //LOG(INFO,"[NetworkHealthScan] browseCallback_(), currentState_ is %d",(int)currentState_);
    auto found = parent_->NodeDB_Find(nodeid);
    if (found == parent_->NodeDB_End())
    {
        LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] browseCallback_(): found == NodeDB_.end()");
        //SyncNotifiable n;
        //snipHelper.SNIPAsync(&snipProcess_,node_,openlcb::NodeHandle(nodeid),
        //                     this);
        //n.wait_for_notification();
        QMember *newitem = new PendingNodeID(nodeid);
        LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] browseCallback_(): newitem is %p",newitem);
        pendingNodeIDs_.insert(newitem);
        LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow] browseCallback_(): pendingNodeIDs_ countains %ld elements",pendingNodeIDs_.pending());
        if (!busy_) notify();
    }
    else
    {
        LOG(INFO,"[NetworkHealthScan] browseCallback_(): found != NodeDB_.end()");
        found->second.status = NetworkNodeDatabaseEntry::Found;
    }
}


void NetworkHealthScan::ResetNodeDB()
{
    LOG(INFO, "[NetworkHealthScan] ResetNodeDB()");
    if (currentState_ == Scanning) return;
    ScanNetwork();
}

#if 0
static bool readline_to_string(int fd,string& buffer)
{
    char c;
    int count = 0;
    buffer = "";
    while (read(fd,&c,1) > 0)
    {
        if (c == '\n') return true;
        buffer += c;
        count++;
    }
    return count > 0;
}
#endif

void NetworkHealthScan::ReadDB_()
{
    NodeDB_.clear();
    //string nodestring;
}


void NetworkHealthScan::WriteDB_()
{
}

void NetworkHealthScan::ScanNetwork()
{
    LOG(INFO, "[NetworkHealthScan] ScanNetwork()");
    if (currentState_ == Scanning) return;
    for (auto it = NodeDB_.begin(); it != NodeDB_.end(); it++)
    {
        it->second.status = NetworkNodeDatabaseEntry::Missing;
    }
    currentState_ = Scanning;
    browsehandleflow_.refresh();
    start(BROWSETIMEOUT);
}

long long NetworkHealthScan::timeout()
{
    LOG(INFO, "[NetworkHealthScan] timeout()");
    currentState_ = ScanComplete;
    found_ = 0;
    missing_ = 0;
    added_ = 0;
    for (auto it = NodeDB_.begin(); it != NodeDB_.end(); it++)
    {
        switch (it->second.status)
        {
        case NetworkNodeDatabaseEntry::Missing:
            missing_++;
            break;
        case NetworkNodeDatabaseEntry::Found:
            found_++;
            break;
        case NetworkNodeDatabaseEntry::New:
            added_++;
            break;
        }
    }
    LOG(INFO, "[NetworkHealthScan] timeout(): Total() = %ld", Total());
    LOG(INFO, "[NetworkHealthScan] timeout(): needWriteDB_ = %d", needWriteDB_);
    if (needWriteDB_)
    {
        WriteDB_();
        needWriteDB_ = false;
    }
    return NONE;
}

StateFlowBase::Action NetworkHealthScan::BrowseHandleFlow::SNIPProcess::entry()
{
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow::SNIPProcess::entry()]");
    return allocate_and_call(&client_,STATE(startSNIP));
}

StateFlowBase::Action NetworkHealthScan::BrowseHandleFlow::SNIPProcess::startSNIP()
{
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow::SNIPProcess::startSNIP()]");
    GetSNIP *m = message()->data();
    buffer_ = client_.alloc(); //get_allocation_result(&client_);
    buffer_->data()->reset(m->src,m->dst);
    buffer_->data()->done.reset(this);
    client_.send(buffer_);
    LOG(INFO,"[NetworkHealthScan::BrowseHandleFlow::SNIPProcess::startSNIP(): client_.send() called.");
    return wait_and_call(STATE(gotSNIP));
}

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


StateFlowBase::Action NetworkHealthScan::BrowseHandleFlow::SNIPProcess::gotSNIP()
{
    string manufacturer("");
    string model("");
    string softwareVersion("");
    string hardwareVersion("");
    string name("");
    string description("");
    size_t index = 0;
    GetSNIP *m = message()->data();
    LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): buffer_->data()->resultCode is %d",buffer_->data()->resultCode);
    openlcb::Payload p = buffer_->data()->response;
    LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): p = %s",HexDumpPayload(p).c_str());
    uint8_t version = p[index++];
    LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): version(1) = %d",version);
    for (size_t i=0; i<version; i++)
    {
        char c = p[index++];
        //LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): c(1) = '%c'",c);
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
    LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): version(2) = %d",version);
    for (size_t i=0; i<version; i++)
    {
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
    LOG(INFO,"[NetworkHealthScan] SNIPProcess::gotSNIP(): 0X%012lX: manufacturer is '%s', model = '%s', softwareVersion = '%s', hardwareVersion = '%s', name = '%s', description = '%s'",m->dst.id,manufacturer.c_str(),model.c_str(),softwareVersion.c_str(),hardwareVersion.c_str(),name.c_str(),description.c_str());
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

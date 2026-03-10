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
//  Last Modified : <260310.1437>
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
#include "openlcb/MemoryConfigClient.hxx"
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
        LOG(INFO,"[NetworkEventScan::BrowseHandleFlow] browseCallback_(): busy_ is %d",busy_);
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
#ifdef DEBUG
    for (auto n = NodeDB_.begin();n != NodeDB_.end(); n++)
    {
        openlcb::NodeID nid = n->first;
        NetworkNodeDatabaseEntry node = n->second;
        LOG(INFO, "[NetworkEventScan] node_loop_start(): 0X%012lX: manufacturer is '%s', model = '%s', softwareVersion = '%s', hardwareVersion = '%s', name = '%s', description = '%s'",nid,node.manufacturer.c_str(),node.model.c_str(),node.softwareVersion.c_str(),node.hardwareVersion.c_str(),node.name.c_str(),node.description.c_str());
    }
#endif
    currentNode_ = NodeDB_.begin();
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
    if (currentNode_ == NodeDB_.end())
    {
        fclose(outfp_);
        ::exit(0);
    }
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] start_load_CDI(): currentNode_->first is 0X%012lX",currentNode_->first);
#endif
    CDI_ = "";
    CDI_Offset = 0;
    MEMBuffer_ = memClient_.alloc(); //get_allocation_result(&memClient_);
    MEMBuffer_->data()->reset(openlcb::MemoryConfigClientRequest::READ_PART, 
                              openlcb::NodeHandle(currentNode_->first), 
                              CDISPACE,CDI_Offset,CDIBLOCKSIZE);
    MEMBuffer_->data()->done.reset(this);
    MEMBuffer_->ref();
    memClient_.send(MEMBuffer_);
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] start_load_CDI(): memClient_.send(%p) called",MEMBuffer_);
#endif
    return wait_and_call(STATE(gotCDIBlock));
}

StateFlowBase::Action NetworkEventScan::gotCDIBlock()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] gotCDIBlock(): MEMBuffer_->data()->resultCode is %d",MEMBuffer_->data()->resultCode);
    LOG(INFO,"[NetworkEventScan] gotCDIBlock(): MEMBuffer_->data()->payload is %s",MEMBuffer_->data()->payload.c_str());
#endif
    CDI_ += MEMBuffer_->data()->payload;
    MEMBuffer_->unref();
    MEMBuffer_ = nullptr;
    auto z = CDI_.find('\0',CDI_Offset);
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] gotCDIBlock(): z = %ld", z);
#endif
    if (z == std::string::npos)
    {
        CDI_Offset += CDIBLOCKSIZE;
        MEMBuffer_ = memClient_.alloc(); //get_allocation_result(&memClient_);`
        MEMBuffer_->data()->reset(openlcb::MemoryConfigClientRequest::READ_PART, 
                                  openlcb::NodeHandle(currentNode_->first), 
                                  CDISPACE,CDI_Offset,CDIBLOCKSIZE);
        MEMBuffer_->data()->done.reset(this);
        MEMBuffer_->ref();
        memClient_.send(MEMBuffer_);
#ifdef DEBUG
        LOG(INFO,"[NetworkEventScan] gotCDIBlock(): memClient_.send(%p) called",MEMBuffer_);
#endif
        return wait_and_call(STATE(gotCDIBlock));
    }
    else
    {
        return call_immediately(STATE(gotCDI));
    }
}

StateFlowBase::Action NetworkEventScan::gotCDI()
{
#ifdef DEBUG
    LOG(INFO,"[NetworkEventScan] gotCDI(): CDI_ is '%s'",CDI_.c_str());
    LOG(INFO,"[NetworkEventScan] gotCDI(): CDI_.size() is %ld", CDI_.size());
#endif
    try {
        parser_.set_throw_messages(true);
        parser_.parse_memory(CDI_);
        if (parser_)
        {
#ifdef DEBUG
            LOG(INFO,"[NetworkEventScan] gotCDI(): CDI Parse successful");
#endif
            processNode_(parser_.get_document()->get_root_node(),-1,"");
        }
    }
    catch (const xmlpp::exception& ex)
    {
        LOG(WARNING,"[NetworkEventScan] gotCDI(): failed to parse CDI for node %s: %s",
            utils::node_id_to_string(currentNode_->first).c_str(),ex.what() );
    }
    return call_immediately(STATE(NextNode));
}

static string reformatEventId(string &payload)
{
    string result = "";
    for (string::size_type i = 0; i < payload.size(); i++)
    {
        char buffer[8];
        snprintf(buffer,sizeof(buffer),"%02X",(uint8_t)payload[i]);
        if (i > 0) result += ".";
        result += buffer;
    }
    return result;
}

void NetworkEventScan::processNode_(xmlpp::Node const* n,int space,
                                    string prefix)
{
    if (n->get_name() == "cdi")
    {
        const xmlpp::Node::NodeList segs = n->get_children("segment");
        for (auto s = segs.begin(); s != segs.end(); s++)
        {
            processNode_(*s,space,prefix);
        }
              
    }
    else if (n->get_name() == "segment")
    {
        auto nodeElement = dynamic_cast<const xmlpp::Element*>(n);
        segmentnumber_++;
        auto spaceattr = nodeElement->get_attribute("space");
        //LOG(INFO,"[NetworkEventScan] processNode_(): spaceattr is %p",spaceattr);
        if (spaceattr != nullptr)
        {
            space = atol(spaceattr->get_value().c_str());
        }
        auto originattr = nodeElement->get_attribute("origin");
        //LOG(INFO,"[NetworkEventScan] processNode_(): originattr is %p",originattr);
        if (originattr != nullptr)
        {
            address_ = atol(originattr->get_value().c_str());
        }
        else
        {
            address_ = 0;
        }
        const xmlpp::Node* name = n->get_first_child ( "name" );
        //LOG(INFO,"[NetworkEventScan] processNode_(): name is %p",name);
        if (name != nullptr)
        {
            const xmlpp::Node* nc = name->get_first_child ( );
            //LOG(INFO,"[NetworkEventScan] processNode_(): nc = %p",nc);
            const auto nodeText = dynamic_cast<const xmlpp::TextNode*>(nc);
            //LOG(INFO,"[NetworkEventScan] processNode_(): nodeText = %p",nodeText);
            prefix = nodeText->get_content();
        }
        else
        {
            char buffer[16];
            snprintf(buffer,sizeof(buffer),"seg%d",segmentnumber_);
            prefix = buffer;
        }
        const xmlpp::Node::NodeList children = n->get_children();
        //LOG(INFO,"[NetworkEventScan] processNode_(): children.size() is %ld",children.size());
        for (auto c = children.begin(); c != children.end(); c++)
        {
            //LOG(INFO,"[NetworkEventScan] processNode_(): *c is %p",*c);
            if ((*c)->get_name() == "name" ||
                (*c)->get_name() == "description") continue;
            processNode_(*c,space,prefix);
        }
    }
    else if (n->get_name() == "group")
    {
        auto nodeElement = dynamic_cast<const xmlpp::Element*>(n);
        unsigned offset = 0;
        auto originattr = nodeElement->get_attribute("origin");
        //LOG(INFO,"[NetworkEventScan] processNode_(): originattr is %p",originattr);
        if (originattr != nullptr)
        {
            offset = atol(originattr->get_value().c_str());
        }
        auto replicationattr = nodeElement->get_attribute("replication");
        unsigned replication = 1;
        if (replicationattr != nullptr)
        {
            replication = atol(replicationattr->get_value().c_str());
        }
        string name = "";
        const xmlpp::Node* nameNode = n->get_first_child ( "name" );
        //LOG(INFO,"[NetworkEventScan] processNode_(): nameNode is %p",name);
        if (nameNode != nullptr)
        {
            const xmlpp::Node* nc = nameNode->get_first_child ( );
            //LOG(INFO,"[NetworkEventScan] processNode_(): nc = %p",nc);
            const auto nodeText = dynamic_cast<const xmlpp::TextNode*>(nc);
            //LOG(INFO,"[NetworkEventScan] processNode_(): nodeText = %p",nodeText);
            name = nodeText->get_content();
        }
        string repnamefmt = "." + name + "(%d)";
        address_ += offset;
        if (replication > 1)
        {
            for (unsigned i = 0; i < replication; i++)
            {
                const xmlpp::Node::NodeList children = n->get_children();
                //LOG(INFO,"[NetworkEventScan] processNode_(): children.size() is %ld",children.size());
                for (auto c = children.begin(); c != children.end(); c++)
                {
                    //LOG(INFO,"[NetworkEventScan] processNode_(): *c is %p",*c);
                    if ((*c)->get_name() == "name" ||
                        (*c)->get_name() == "description" ||
                        (*c)->get_name() == "repname" ||
                        (*c)->get_name() == "hints") continue;
                    char buffer[128];
                    snprintf(buffer,sizeof(buffer),repnamefmt.c_str(),i);
                    processNode_(*c,space,prefix + buffer);
                }
            }
        }
        else
        {
            const xmlpp::Node::NodeList children = n->get_children();
            //LOG(INFO,"[NetworkEventScan] processNode_(): children.size() is %ld",children.size());
            for (auto c = children.begin(); c != children.end(); c++)
            {
                //LOG(INFO,"[NetworkEventScan] processNode_(): *c is %p",*c);
                if ((*c)->get_name() == "name" ||
                    (*c)->get_name() == "description" ||
                    (*c)->get_name() == "repname" ||
                    (*c)->get_name() == "hints") continue;
                processNode_(*c,space,prefix + "." + name);
            }
        }
    }
    else if (n->get_name() == "eventid")
    {
        auto nodeElement = dynamic_cast<const xmlpp::Element*>(n);
        unsigned offset = 0;
        auto originattr = nodeElement->get_attribute("origin");
        //LOG(INFO,"[NetworkEventScan] processNode_(): originattr is %p",originattr);
        if (originattr != nullptr)
        {
            offset = atol(originattr->get_value().c_str());
        }
        address_ += offset;
        string name = "";
        const xmlpp::Node* nameNode = n->get_first_child ( "name" );
        //LOG(INFO,"[NetworkEventScan] processNode_(): nameNode is %p",name);
        if (nameNode != nullptr)
        {
            const xmlpp::Node* nc = nameNode->get_first_child ( );
            //LOG(INFO,"[NetworkEventScan] processNode_(): nc = %p",nc);
            const auto nodeText = dynamic_cast<const xmlpp::TextNode*>(nc);
            //LOG(INFO,"[NetworkEventScan] processNode_(): nodeText = %p",nodeText);
            name = nodeText->get_content();
        }
        string description = "";
        const xmlpp::Node* descriptionNode = n->get_first_child ( "description" );
        //LOG(INFO,"[NetworkEventScan] processNode_(): descriptionNode is %p",descriptionNode);
        if (descriptionNode != nullptr)
        {
            const xmlpp::Node* dc = descriptionNode->get_first_child ( );
            //LOG(INFO,"[NetworkEventScan] processNode_(): dc = %p",dc);
            const auto nodeText = dynamic_cast<const xmlpp::TextNode*>(dc);
            //LOG(INFO,"[NetworkEventScan] processNode_(): nodeText = %p",nodeText);
            description = nodeText->get_content();
        }
        
        unsigned size = 8;
        uint8_t spacearg = space;
        auto buff = invoke_flow(&memClient_,
                                openlcb::MemoryConfigClientRequest::READ_PART,
                                openlcb::NodeHandle(currentNode_->first), 
                                spacearg,address_,size);
        string eventidstring = reformatEventId(buff->data()->payload);
        buff->unref();
        csv_fwrite(outfp_,eventidstring.c_str(),eventidstring.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,name.c_str(),name.size());
        fputc(',',outfp_);
        string nid = utils::node_id_to_string(currentNode_->first);
        csv_fwrite(outfp_,nid.c_str(),nid.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,currentNode_->second.name.c_str(),currentNode_->second.name.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,currentNode_->second.model.c_str(),currentNode_->second.model.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,currentNode_->second.manufacturer.c_str(),currentNode_->second.manufacturer.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,prefix.c_str(),prefix.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,description.c_str(),description.size());
        fputc(',',outfp_);
        csv_fwrite(outfp_,"",0);
        fputc(',',outfp_);
        csv_fwrite(outfp_,"",0);
        fputc('\n',outfp_);
        address_ += size;
    }
    else if (n->get_name() == "int" ||
             n->get_name() == "string")
    {
        unsigned offset = 0;
        unsigned size = 1;
        auto nodeElement = dynamic_cast<const xmlpp::Element*>(n);
        auto originattr = nodeElement->get_attribute("origin");
        //LOG(INFO,"[NetworkEventScan] processNode_(): originattr is %p",originattr);
        if (originattr != nullptr)
        {
            offset = atol(originattr->get_value().c_str());
        }
        address_ += offset;
        auto sizeattr = nodeElement->get_attribute("size");
        //LOG(INFO,"[NetworkEventScan] processNode_(): sizeattr is %p",sizeattr);
        if (sizeattr != nullptr)
        {
            size = atol(sizeattr->get_value().c_str());
        }
        address_ += size;
    }
    else
    {
    }
}

StateFlowBase::Action NetworkEventScan::NextNode()
{
    currentNode_++;
    return call_immediately(STATE(start_load_CDI));
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

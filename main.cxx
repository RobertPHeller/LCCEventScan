// -!- C++ -!- //////////////////////////////////////////////////////////////
//
//  System        : 
//  Module        : 
//  Object Name   : $RCSfile$
//  Revision      : $Revision$
//  Date          : $Date$
//  Author        : $Author$
//  Created By    : Robert Heller
//  Created       : 2026-03-05 15:21:27
//  Last Modified : <260309.2041>
//
//  Description	
//
//  Notes
//
//  History
//	
/////////////////////////////////////////////////////////////////////////////
/** @copyright
 *     Copyright (C) 2026  Robert Heller D/B/A Deepwoods Software
 * 			51 Locke Hill Road
 * 			Wendell, MA 01379-9728
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  @file main.cxx
 * 
 *  Main file for the LCC Event scanner
 * 
 *  @author Robert Heller
 *  @date 2026-03-05 15:21:27
 *  @page LCCEventScan LCC Event Scan Command Line
 * 
 * @section SYNOPSIS SYNOPSIS
 * 
 * LCCEventScan [options] outputfile
 * 
 * @section DESCRIPTION DESCRIPTION
 * 
 * This program will scan all of the nodes on a LCC Network and for all
 * nodes that provide both the CDI and Event Exchange protocols will read
 * the CDI and extract all of the EventIDs defined and create a CSV file
 * containing those Event IDs, along with various bits of info relating to 
 * them.
 * 
 * @section OPTIONS OPTIONS
 * 
 * @arg -n nodeid is the node id, as a 12 hex digit number (optionally with 
 * colons between pairs of hex digits).  Defaults to 05:01:01:01:22:00.
 * @arg -u upstream_host is the host name for an upstream hub.
 * @arg -q upstream_port is the port number for the upstream hub. Defaults
 * to 12021.
 * @arg -c can_socketname is the name of the CAN socket.
 * @arg -t cantty USB/TTY port of a USB/GC<=>CAN device, such as a RR-CitKits 
 * LCC Buffer-USB.
 * @par
 * 
 * At least one of the connection options needs to be specified.  There is no
 * default connection option.
 * 
 * @section PARAMETERS PARAMETERS
 * 
 * The name of the output file.  This parameter is required.
 * 
 *  @section FILES FILES
 * None
 * 
 * @mainpage Introduction
 * 
 * This program was inspired by an E-Mail message on the LCC groups.io list
 * posted by Dwight Kelly.  This program scans a LCC network looking for 
 * active nodes that support the CDI and Event Transfer protocols.  It 
 * collects the SNIP infor for these nodes (make, model, hardware and software
 *  versions, names and descriptions then goes through this list of nodes
 * fetching the node's CDI and then collecting all of the nodes EventIds. It
 * creates a CSV file containing the EventIds, with the information about
 * the EventIds.  This file can be processed by other software to possibly
 * automate the process of configuring the nodes.
 * 
 */

static const char rcsid[] = "@(#) : $Id$";


#include <ctype.h>
#include "os/os.h"
#include "nmranet_config.h"

#include "openlcb/SimpleStack.hxx"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "NetworkEventScan.hxx"

// Changes the default behavior by adding a newline after each gridconnect
// packet. Makes it easier for debugging the raw device.
OVERRIDE_CONST(gc_generate_newlines, 1);
// Specifies how much RAM (in bytes) we allocate to the stack of the main
// thread. Useful tuning parameter in case the application runs out of memory.
OVERRIDE_CONST(main_thread_stack_size, 10000);
// Specifies the default 48-bit OpenLCB node identifier. This must be unique for every
// hardware manufactured, so in production this should be replaced by some
// easily incrementable method.
#define DefaultNODEID 0x050101012200ULL // 05 01 01 01 22 00
static openlcb::NodeID NODE_ID = DefaultNODEID;
bool UseGCHost = false;
int upstream_port = 12021;
const char *upstream_host = "localhost";
bool UseCANSocket = false;
const char *cansocket = "can0";
bool UseUSBTTy = false;
const char *usb_gc_port = "/dev/ttyACM0";


extern const char *const openlcb::SNIP_DYNAMIC_FILENAME = NULL;

extern const openlcb::SimpleNodeStaticValues SNIP_STATIC_DATA = {
    4, "Deepwoods Software", "LCCEventScan", "", "1.0"};


// CLI Usage output.

void usage(const char *e)
{
    fprintf(stderr, "Usage: %s [-n nodeid] [-u upstream_host] [-q upstream_port] [-c can_socketname] [-t usbtty] outputfile\n",e);
    fprintf(stderr, "\n\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "\t-n nodeid is the node id, as a 12 hex digit number (optionally with colons between pairs of hex digits.\n");
    fprintf(stderr,"\t-u upstream_host   is the host name for an "
            "upstream hub.\n");
    fprintf(stderr,
            "\t-q upstream_port   is the port number for the upstream "
            "hub.\n");
    fprintf(stderr,"\t-c can_socketname   is the name of the CAN "
            "socket.\n");
    fprintf(stderr,"\t-t cantty USB/TTY port of a USB/GC<=>CAN device\n");
    exit(1);
}
// Parse CLI options.

openlcb::NodeID parseNodeID(const char *nidstring)
{
    uint64_t result = 0ULL;
    int nibcount = 0, coloncount = 0;
    const char *p = NULL;
    for (p = nidstring; *p != '\0'; p++)
    {
        if (isxdigit(*p))
        {
            nibcount++;
            if (isdigit(*p))
            {
                result = (result<<4)+(*p-'0');
            }
            else if (islower(*p))
            {
                result = (result<<4)+(*p-'a'+10);
            }
            else
            {
                result = (result<<4)+(*p-'A'+10);
            }
        }
        else if (*p == ':')
        {
            coloncount++;
        }
        else
        {
            // not a hex digit or colon
            fprintf(stderr, "Syntax error: Illformed node id: %s\n",nidstring);
            return (openlcb::NodeID) -1;
        }
    }
    if (nibcount != 12)
    {
        // Wrong number of digits
        fprintf(stderr, "Syntax error: Illformed node id: %s\n",nidstring);
        return (openlcb::NodeID) -1;
    }
    if (coloncount != 0 && coloncount != 5)
    {
        // Wrong number of colons (some number other than 0 or 5)
        fprintf(stderr, "Syntax error: Illformed node id: %s\n",nidstring);
        return (openlcb::NodeID) -1;
    }
    return (openlcb::NodeID) result;
}

void parse_args(int argc, char *argv[])
{
    int opt;
#define OPTSTRING "hn:u:q:c:t:"
    while ((opt = getopt(argc, argv, OPTSTRING)) >= 0)
    {
        switch (opt)
        {
        case 'h':
            usage(argv[0]);
            break;
        case 'n':
            {
                openlcb::NodeID nid = parseNodeID(optarg);
                if (((int64_t)nid) == -1) 
                {
                    usage(argv[0]);
                }
                else
                {
                    NODE_ID = nid;
                }
            }
            break;
        case 'u':
            upstream_host = optarg;
            UseGCHost = true;
            break;
        case 'q':
            upstream_port = atoi(optarg);
            UseGCHost = true;
            break;
        case 'c':
            cansocket = optarg;
            UseCANSocket = true;
            break;
        case 't':
            usb_gc_port = optarg;
            UseUSBTTy = true;
            break;
        default:
            fprintf(stderr, "Unknown option %c\n", opt);
            usage(argv[0]);
        }
    }
}
/** Entry point to application.
 * @param argc number of command line arguments
 * @param argv array of command line arguments
 * @return 0, should never return
 */
int appl_main(int argc, char *argv[])
{
    // Parse command line.
    parse_args(argc, argv);
    if (optind >= argc) {
        LOG(FATAL,"Missing output filename");
    }
    openlcb::SimpleCanStack stack(NODE_ID);
    bool isconnected = false;
    
    if (UseGCHost)
    {
        stack.connect_tcp_gridconnect_hub(upstream_host, upstream_port);
        isconnected = true;
    }
    if (UseCANSocket)
    {
        stack.add_socketcan_port_select(cansocket);
        isconnected = true;
    }
    if (UseUSBTTy)
    {
        stack.add_gridconnect_tty(usb_gc_port);
        isconnected = true;
    }
    if (!isconnected)
    {
        LOG(FATAL,"Not connected to a CAN network!");
    }
    Executor<1> netscan_executor("netscan_executor", 0, 8192);
    NetworkEventScan::NetworkEventScanThread
          networkScan(&netscan_executor,stack.node(),
                     stack.memory_config_handler(),
                     argv[optind]);
    //healthScan.ScanNetwork();
    // Start the stack in the background using it's own task.
    stack.loop_executor();
    // At this point the OpenMRN stack is running in it's own task and we can
    // safely exit from this one.
    return 0;
}


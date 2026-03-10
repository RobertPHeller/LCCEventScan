# LCC Event Scan
## Introduction
 
This program was inspired by an E-Mail message on the LCC groups.io list
posted by Dwight Kelly.  This program scans a LCC network looking for 
active nodes that support the CDI and Event Transfer protocols.  It 
collects the SNIP infor for these nodes (make, model, hardware and software
versions, names and descriptions then goes through this list of nodes
fetching the node's CDI and then collecting all of the nodes EventIds. It
creates a CSV file containing the EventIds, with the information about
the EventIds.  This file can be processed by other software to possibly
automate the process of configuring the nodes.

## Building

The program is in C++ and uses GNU Makefiles under Linux. It uses the OpenMRN
framework,  so you need a C++ compiler  toolchain,  GNU Make, and OpenMRN.  It
also uses two additional libraries: libcsv and libxml++, both of which are 
standard packages on most Linux systems.


## LCC Event Scan Command Line
 
### SYNOPSIS
 
LCCEventScan [options] outputfile
 
### DESCRIPTION
 
This program will scan all of the nodes on a LCC Network and for all
nodes that provide both the CDI and Event Exchange protocols will read
the CDI and extract all of the EventIDs defined and create a CSV file
containing those Event IDs, along with various bits of info relating to 
them.

### OPTIONS
 
- -n nodeid is the node id, as a 12 hex digit number (optionally with 
colons between pairs of hex digits).  Defaults to 05:01:01:01:22:00.
- -u upstream_host is the host name for an upstream hub.
- -q upstream_port is the port number for the upstream hub. Defaults
to 12021.
- -c can_socketname is the name of the CAN socket.
- -t cantty USB/TTY port of a USB/GC<=>CAN device, such as a RR-CitKits 
LCC Buffer-USB.

At least one of the connection options needs to be specified.  There is no
default connection option.

### PARAMETERS
 
The name of the output file.  This parameter is required.
 

// -!- c++ -!- //////////////////////////////////////////////////////////////
//
//  System        : 
//  Module        : 
//  Object Name   : $RCSfile$
//  Revision      : $Revision$
//  Date          : $Date$
//  Author        : $Author$
//  Created By    : Robert Heller
//  Created       : Sun Dec 18 12:05:25 2022
//  Last Modified : <260309.2045>
//
//  Description	
//
//  Notes
//
//  History
//	
/////////////////////////////////////////////////////////////////////////////
/// @copyright
///    Copyright (C) 2022  Robert Heller D/B/A Deepwoods Software
///			51 Locke Hill Road
///			Wendell, MA 01379-9728
///
///    This program is free software; you can redistribute it and/or modify
///    it under the terms of the GNU General Public License as published by
///    the Free Software Foundation; either version 2 of the License, or
///    (at your option) any later version.
///
///    This program is distributed in the hope that it will be useful,
///    but WITHOUT ANY WARRANTY; without even the implied warranty of
///    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///    GNU General Public License for more details.
///
///    You should have received a copy of the GNU General Public License
///    along with this program; if not, write to the Free Software
///    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
/// @file StringUtils.hxx
/// @author Robert Heller 
/// @date Sun Dec 18 12:05:25 2022
//////////////////////////////////////////////////////////////////////////////

#ifndef __STRINGUTILS_HXX
#define __STRINGUTILS_HXX

#include <string>
#include <string.h>

using std::string;

/** Namespace used to hold a couple of simple string utils for converting
 * node ids to and from strings.
 */
namespace utils
{
/** Convert a node id to a hexidecimal string representation. */
string node_id_to_string(uint64_t node_id);
/** Convert a hexidecimal string representation of a node id to a binary
 * representation.
 */
uint64_t string_to_uint64(const string node_string);

}

#endif // __STRINGUTILS_HXX


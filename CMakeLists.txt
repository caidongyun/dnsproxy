# This code is part of Pcap_DNSProxy
# A local DNS server based on WinPcap and LibPcap
# Copyright (C) 2012-2017 Chengr28
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


# CMake minimum version
IF(PLATFORM_OPENWRT)
	cmake_minimum_required(VERSION 2.8.12.2)
ELSE(PLATFORM_OPENWRT)
	cmake_minimum_required(VERSION 3.0.2)
ENDIF(PLATFORM_OPENWRT)

# Sub directories
add_subdirectory(Source/Pcap_DNSProxy)

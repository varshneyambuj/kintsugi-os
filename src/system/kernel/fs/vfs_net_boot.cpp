/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2007, Ingo Weinhold, bonefish@cs.tu-berlin.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file vfs_net_boot.cpp
 * @brief Network boot support for VFS — mounts the root file system over NFS during net-boot.
 */

#include "vfs_net_boot.h"

#include <dirent.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <DiskDeviceTypes.h>

#include <disk_device_manager/KDiskDevice.h>

#include <KPath.h>


/** @brief Test whether a string begins with a given prefix.
 *
 * @param string  The string to test.
 * @param prefix  The prefix to look for.
 * @return @c true if @p string starts with @p prefix, @c false otherwise.
 */
static bool
string_starts_with(const char* string, const char* prefix)
{
	size_t stringLen = strlen(string);
	size_t prefixLen = strlen(prefix);
	return (stringLen >= prefixLen && strncmp(string, prefix, prefixLen) == 0);
}


/** @brief Determine whether a disk device is a virtual network-backed device.
 *
 * Checks whether the device path starts with either the NBD or RemoteDisk
 * virtual-disk prefix.
 *
 * @param device  Pointer to the KDiskDevice to inspect.
 * @return @c true if the device is an NBD or RemoteDisk device.
 */
static bool
is_net_device(KDiskDevice* device)
{
	const char* path = device->Path();
	return (string_starts_with(path, "/dev/disk/virtual/nbd/")
		|| string_starts_with(path, "/dev/disk/virtual/remote_disk/"));
}


/** @brief Comparator for qsort() that orders partitions so that network-backed
 *         devices sort after locally-backed ones.
 *
 * Partitions on network devices are ranked higher (later) than those on local
 * devices.  Partitions within the same class are further ordered by
 * compare_image_boot().
 *
 * @param _a  Pointer to the first @c KPartition* element.
 * @param _b  Pointer to the second @c KPartition* element.
 * @return Negative if @p _a should come first, positive if @p _b should come
 *         first, zero if they are equivalent.
 */
static int
compare_partitions_net_devices(const void *_a, const void *_b)
{
	KPartition* a = *(KPartition**)_a;
	KPartition* b = *(KPartition**)_b;

	bool aIsNetDevice = is_net_device(a->Device());
	bool bIsNetDevice = is_net_device(b->Device());

	int compare = (int)aIsNetDevice - (int)bIsNetDevice;
	if (compare != 0)
		return compare;

	return compare_image_boot(_a, _b);
}


/** @brief Helper class that brings up the IP network stack for net-boot.
 *
 * Scans /dev/net for ethernet interfaces, matches one by MAC address, and
 * assigns the client IP address, network mask, broadcast address, and default
 * route provided by the boot loader.
 */
class NetStackInitializer {
public:
	/** @brief Construct a NetStackInitializer with the boot-loader supplied parameters.
	 *
	 * @param clientMAC  48-bit Ethernet MAC address of the boot interface, packed
	 *                   into the low 48 bits of a uint64.
	 * @param clientIP   IPv4 address to assign to the interface (host byte order).
	 * @param netMask    IPv4 network mask to assign (host byte order).
	 */
	NetStackInitializer(uint64 clientMAC, uint32 clientIP, uint32 netMask)
		:
		fSocket(-1),
		fLinkSocket(-1),
		fClientMAC(clientMAC),
		fClientIP(clientIP),
		fNetMask(netMask),
		fFoundInterface(false),
		fConfiguredInterface(false)
	{
	}

	/** @brief Destructor — closes any open control sockets. */
	~NetStackInitializer()
	{
		// close control sockets
		if (fSocket >= 0)
			close(fSocket);

		if (fLinkSocket >= 0)
			close(fLinkSocket);
	}

	/** @brief Open control sockets and scan /dev/net for the boot interface.
	 *
	 * Opens an AF_INET and an AF_LINK control socket, then recursively scans
	 * /dev/net until an ethernet interface whose MAC matches fClientMAC is
	 * found and fully configured.
	 *
	 * @return B_OK if the boot interface was found and configured successfully,
	 *         @c errno on socket failure, or B_ERROR if no matching interface
	 *         was found.
	 */
	status_t Init()
	{
		// open a control socket for playing with the stack
		fSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (fSocket < 0) {
			dprintf("NetStackInitializer: Failed to open socket: %s\n",
				strerror(errno));
			return errno;
		}

		// ... and a link level socket
		fLinkSocket = socket(AF_LINK, SOCK_DGRAM, 0);
		if (fLinkSocket < 0) {
			dprintf("NetStackInitializer: Failed to open link level socket:"
				" %s\n", strerror(errno));
			return errno;
		}


		// now iterate through the existing network devices
		KPath path;
		status_t error = path.SetTo("/dev/net");
		if (error != B_OK)
			return error;

		_ScanDevices(path);

		return fConfiguredInterface ? B_OK : B_ERROR;
	}

private:
	/** @brief Recursively scan a directory tree under /dev/net for network devices.
	 *
	 * For each entry that is a directory, recurses; for each entry that is a
	 * character or block device, calls _ScanDevice().  Stops scanning as soon
	 * as fFoundInterface becomes @c true.
	 *
	 * @param path  Current directory path; modified in place (leaf appended and
	 *              removed during recursion).
	 */
	void _ScanDevices(KPath& path)
	{
		DIR* dir = opendir(path.Path());
		if (!dir) {
			dprintf("NetStackInitializer: Failed to opendir() \"%s\": %s\n",
				path.Path(), strerror(errno));
			return;
		}

		while (dirent* entry = readdir(dir)) {
			// skip "." and ".."
			if (strcmp(entry->d_name, ".") == 0
				|| strcmp(entry->d_name, "..") == 0) {
				continue;
			}

			path.Append(entry->d_name);

			struct stat st;
			if (stat(path.Path(), &st) == 0) {
				if (S_ISDIR(st.st_mode))
					_ScanDevices(path);
				else if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode))
					_ScanDevice(path.Path());
			}

			path.RemoveLeaf();

			if (fFoundInterface)
				break;
		}

		closedir(dir);
	}

	/** @brief Attempt to configure a single network device as the boot interface.
	 *
	 * Adds the interface to the stack if not already present, brings it up,
	 * reads its MAC address, and — if the MAC matches fClientMAC — assigns the
	 * IP address, network mask, broadcast address, and a default static route.
	 * Sets fFoundInterface and fConfiguredInterface on success.
	 *
	 * @param path  Kernel device path (e.g. "/dev/net/ipro1000/0").
	 */
	void _ScanDevice(const char* path)
	{
		dprintf("NetStackInitializer: scanning device %s\n", path);

		// check if this interface is already known
		ifreq request;
		if (strlen(path) >= IF_NAMESIZE)
			return;
		strcpy(request.ifr_name, path);

		if (ioctl(fSocket, SIOCGIFINDEX, &request, sizeof(request)) < 0) {
			// not known yet -- add it
			ifaliasreq aliasRequest;
			strcpy(aliasRequest.ifra_name, path);
			aliasRequest.ifra_addr.ss_family = AF_UNSPEC;
			aliasRequest.ifra_addr.ss_len = 2;
			aliasRequest.ifra_broadaddr.ss_family = AF_UNSPEC;
			aliasRequest.ifra_broadaddr.ss_len = 2;
			aliasRequest.ifra_mask.ss_family = AF_UNSPEC;
			aliasRequest.ifra_mask.ss_len = 2;

			if (ioctl(fSocket, SIOCAIFADDR, &aliasRequest,
					sizeof(aliasRequest)) < 0) {
				dprintf("NetStackInitializer: adding interface failed for "
					"device %s: %s\n", path, strerror(errno));
				return;
			}
		}

		// bring the interface up (get flags, add IFF_UP)
		if (ioctl(fSocket, SIOCGIFFLAGS, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: getting flags failed for interface "
				"%s: %s\n", path, strerror(errno));
			return;
		}

		int interfaceFlags = request.ifr_flags;
		if (!(interfaceFlags & IFF_UP)) {
			interfaceFlags |= IFF_UP;
			request.ifr_flags = interfaceFlags;
			if (ioctl(fSocket, SIOCSIFFLAGS, &request, sizeof(request)) < 0) {
				dprintf("NetStackInitializer: failed to bring interface up "
					"%s: %s\n", path, strerror(errno));
				return;
			}
		}

		// get the MAC address
		if (ioctl(fLinkSocket, SIOCGIFADDR, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: Getting MAC addresss failed for "
				"interface %s: %s\n", path, strerror(errno));
			return;
		}

		sockaddr_dl& link = *(sockaddr_dl*)&request.ifr_addr;
		if (link.sdl_type != IFT_ETHER)
			return;

		if (link.sdl_alen == 0)
			return;

		uint8* macBytes = (uint8 *)LLADDR(&link);
		uint64 macAddress = ((uint64)macBytes[0] << 40)
			| ((uint64)macBytes[1] << 32)
			| ((uint64)macBytes[2] << 24)
			| ((uint64)macBytes[3] << 16)
			| ((uint64)macBytes[4] << 8)
			| (uint64)macBytes[5];

		dprintf("NetStackInitializer: found ethernet interface with MAC "
			"address %02x:%02x:%02x:%02x:%02x:%02x; which is%s the one we're "
			"looking for\n", macBytes[0], macBytes[1], macBytes[2], macBytes[3],
			macBytes[4], macBytes[5], (macAddress == fClientMAC ? "" : "n't"));

		if (macAddress != fClientMAC)
			return;

		fFoundInterface = true;

		// configure the interface

		// set IP address
		sockaddr_in& address = *(sockaddr_in*)&request.ifr_addr;
		address.sin_family = AF_INET;
		address.sin_len = sizeof(sockaddr_in);
		address.sin_port = 0;
		address.sin_addr.s_addr = htonl(fClientIP);
		memset(&address.sin_zero[0], 0, sizeof(address.sin_zero));
		if (ioctl(fSocket, SIOCSIFADDR, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: Setting IP addresss failed for "
				"interface %s: %s\n", path, strerror(errno));
			return;
		}

		// set net mask
		address.sin_addr.s_addr = htonl(fNetMask);
		if (ioctl(fSocket, SIOCSIFNETMASK, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: Setting net mask failed for "
				"interface %s: %s\n", path, strerror(errno));
			return;
		}

		// set broadcast address
		address.sin_addr.s_addr = htonl(fClientIP | ~fNetMask);
		if (ioctl(fSocket, SIOCSIFBRDADDR, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: Setting broadcast address failed for "
				"interface %s: %s\n", path, strerror(errno));
			return;
		}

		// set IFF_BROADCAST
		if (!(interfaceFlags & IFF_BROADCAST)) {
			interfaceFlags |= IFF_BROADCAST;
			request.ifr_flags = interfaceFlags;
			if (ioctl(fSocket, SIOCSIFFLAGS, &request, sizeof(request)) < 0) {
				dprintf("NetStackInitializer: failed to set IFF_BROADCAST flag "
					"for interface %s: %s\n", path, strerror(errno));
				return;
			}
		}

		// set default route; remove previous one, if any
		route_entry route;
		memset(&route, 0, sizeof(route_entry));
		route.flags = RTF_STATIC | RTF_DEFAULT;

		request.ifr_route = route;
		ioctl(fSocket, SIOCDELRT, &request, sizeof(request));
		if (ioctl(fSocket, SIOCADDRT, &request, sizeof(request)) < 0) {
			dprintf("NetStackInitializer: Failed to set default route: %s\n",
				strerror(errno));
			return;
		}

		fConfiguredInterface = true;

		dprintf("NetStackInitializer: successfully configured boot network "
			"interface\n");
	}

private:
	int					fSocket;
	int					fLinkSocket;
	uint64				fClientMAC;
	uint32				fClientIP;
	uint32				fNetMask;
	bool				fFoundInterface;
	bool				fConfiguredInterface;
};


// #pragma mark - NetBootMethod


/** @brief Construct a NetBootMethod with the boot-volume message and boot method ID.
 *
 * @param bootVolume  Reference to the KMessage containing boot parameters
 *                    passed by the boot loader.
 * @param method      Numeric boot-method identifier.
 */
NetBootMethod::NetBootMethod(const KMessage& bootVolume, int32 method)
	: BootMethod(bootVolume, method)
{
}


/** @brief Destructor for NetBootMethod. */
NetBootMethod::~NetBootMethod()
{
}


/** @brief Initialise the network boot method.
 *
 * Reads the client MAC address, IP address, and network mask from the boot
 * volume message (choosing a classful default mask when none is present),
 * then invokes NetStackInitializer to bring up the matching network interface.
 *
 * @return B_OK if the network interface was configured successfully,
 *         B_ERROR if required boot parameters are missing, or any error
 *         returned by NetStackInitializer::Init().
 */
status_t
NetBootMethod::Init()
{
	// We need to bring up the net stack.
	status_t status;

	uint64 clientMAC;
	uint32 clientIP = 0;
	uint32 netMask;
	if (fBootVolume.FindInt64("client MAC", (int64*)&clientMAC) != B_OK
		|| fBootVolume.FindInt32("client IP", (int32*)&clientIP) != B_OK) {
		panic("no client MAC or IP address or net mask\n");
		return B_ERROR;
	}

	if (fBootVolume.FindInt32("net mask", (int32*)&netMask) != B_OK) {
		// choose default netmask depending on the class of the address
		if (IN_CLASSA(clientIP)
			|| (clientIP >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) {
			// class A, or loopback
			netMask = IN_CLASSA_NET;
		} else if (IN_CLASSB(clientIP)) {
			// class B
			netMask = IN_CLASSB_NET;
		} else {
			// class C and rest
			netMask = IN_CLASSC_NET;
		}
	}

	NetStackInitializer initializer(clientMAC, clientIP, netMask);
	status = initializer.Init();
	if (status != B_OK)
		return status;

	// TODO: "net root path" should be used for finding the boot device/FS,
	// but ATM neither the remote_disk nor the nbd driver are configurable
	// at this point.
	const char* rootPath = fBootVolume.GetString("net root path", NULL);
	dprintf("NetBootMethod::Init(): net stack initialized; root path is: %s\n",
		rootPath);

	return B_OK;
}


/** @brief Determine whether a disk device is a valid net-boot boot device.
 *
 * Accepts only NBD and RemoteDisk virtual disk devices.
 *
 * @param device  The disk device to test.
 * @param strict  Unused; present to match the BootMethod interface.
 * @return @c true if the device path is under the NBD or RemoteDisk subtree.
 */
bool
NetBootMethod::IsBootDevice(KDiskDevice* device, bool strict)
{
	// We support only NBD and RemoteDisk at the moment, so we accept any
	// device under /dev/disk/virtual/{nbd,remote_disk}/.
	return is_net_device(device);
}


/** @brief Determine whether a partition is a valid net-boot root partition.
 *
 * Accepts any BFS-formatted partition unconditionally; sets foundForSure to
 * @c false (the caller should keep looking).
 *
 * @param partition     The partition to test.
 * @param foundForSure  Always left unchanged (not set to @c true).
 * @return @c true if the partition content type is BFS.
 */
bool
NetBootMethod::IsBootPartition(KPartition* partition, bool& foundForSure)
{
	// as long as it's BFS, we're fine
	return (partition->ContentType()
		&& strcmp(partition->ContentType(), kPartitionTypeBFS) == 0);
}


/** @brief Sort a partition list so that network-backed partitions come last.
 *
 * Delegates to compare_partitions_net_devices() via qsort().
 *
 * @param partitions  Array of KPartition pointers to sort in place.
 * @param count       Number of elements in @p partitions.
 */
void
NetBootMethod::SortPartitions(KPartition** partitions, int32 count)
{
	qsort(partitions, count, sizeof(KPartition*),
		compare_partitions_net_devices);
}
